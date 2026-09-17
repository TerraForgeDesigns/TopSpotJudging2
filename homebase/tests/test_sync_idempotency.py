"""
Sync idempotency — see PROTOCOL.md's Idempotency section. Exercised
through the real POST /api/v1/sync endpoint (not by calling a service
function directly) so this also proves out the wire contract end to end,
not just the underlying logic.

The unified sync endpoint does not exist yet as of this commit — these
tests are written against the INTENDED behavior first (see this task's
execution order) and are expected to fail until Step 6.
"""
from datetime import date

import pytest
from sqlalchemy import select

from app.models import Car, Handheld, JudgingCategory, JudgingScore, JudgingSubmission, Show, SubmissionStatus
from app.services.shows import set_active_show


@pytest.fixture()
def show_with_car(db_session):
    # services/shows.py no longer has create_show() — show creation moved
    # to services/show_wizard.py's materialize() (see DECISIONS.md). This
    # test is about sync, not the wizard, so it builds the Show directly
    # rather than going through either creation path.
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field")
    db_session.add(show)
    db_session.commit()
    set_active_show(db_session, show.id)
    category = JudgingCategory(show_id=show.id, name="Engine", sort_order=1, active=True)
    db_session.add(category)
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=show.show_data_revision)
    db_session.add(car)
    db_session.commit()
    return show, category, car


def _submit(client, handheld_id, entry_number, closed_at_uptime_ms, category_id, points, score_range_max=5):
    payload = {
        "handheld_id": handheld_id,
        "config_revision": 0,
        "data_revision": 0,
        "battery_pct": 80,
        "submissions": [
            {
                "entry_number": entry_number,
                "closed_at_uptime_ms": closed_at_uptime_ms,
                "score_range_max": score_range_max,
                "scores": [{"category_id": category_id, "points": points}],
                "vehicle_photo_path": "/sdcard/topspot/photos/vehicle/test.jpg",
                "judge_sheet_photo_path": "/sdcard/topspot/photos/judge_sheets/test.jpg",
            }
        ],
    }
    response = client.post("/api/v1/sync", json=payload)
    assert response.status_code == 200, response.text
    return response.json()


def test_retry_with_same_triple_is_already_recorded_and_creates_nothing(client, db_session, show_with_car):
    show, category, car = show_with_car

    first = _submit(client, "hh-1", "001", closed_at_uptime_ms=1000, category_id=category.id, points=3)
    assert first["results"][0]["status"] == "accepted"

    second = _submit(client, "hh-1", "001", closed_at_uptime_ms=1000, category_id=category.id, points=3)
    assert second["results"][0]["status"] == "already_recorded"

    submissions = list(db_session.scalars(select(JudgingSubmission).where(JudgingSubmission.car_id == car.id)))
    assert len(submissions) == 1


def test_different_handheld_same_entry_is_flagged_duplicate_and_leaves_original_untouched(
    client, db_session, show_with_car
):
    show, category, car = show_with_car

    first = _submit(client, "hh-1", "001", closed_at_uptime_ms=1000, category_id=category.id, points=3)
    assert first["results"][0]["status"] == "accepted"

    second = _submit(client, "hh-2", "001", closed_at_uptime_ms=2000, category_id=category.id, points=5)
    assert second["results"][0]["status"] == "flagged_duplicate"

    submissions = list(
        db_session.scalars(
            select(JudgingSubmission).where(JudgingSubmission.car_id == car.id).order_by(JudgingSubmission.id)
        )
    )
    assert len(submissions) == 2
    original = submissions[0]
    assert original.status == SubmissionStatus.ACCEPTED
    assert original.scores[0].original_points == 3  # untouched by the second handheld's submission

    duplicate = submissions[1]
    assert duplicate.status == SubmissionStatus.FLAGGED_DUPLICATE


def test_stale_score_range_max_is_converted_on_receipt(client, db_session, show_with_car):
    """The show has already escalated to 1-10; a handheld that hasn't
    caught up yet submits scored at 1-5. Home Base must convert on
    receipt and keep the original — see PROTOCOL.md."""
    show, category, car = show_with_car
    show.score_range_max = 10
    db_session.commit()

    result = _submit(client, "hh-1", "001", closed_at_uptime_ms=1000, category_id=category.id, points=3, score_range_max=5)
    assert result["results"][0]["status"] == "accepted"

    submission = db_session.scalars(select(JudgingSubmission).where(JudgingSubmission.car_id == car.id)).first()
    score = submission.scores[0]
    assert score.original_points == 3
    assert score.original_range_max == 5
    assert score.adjusted_points == 6  # double the original — 3 * 10/5


def test_configuration_carries_a_server_computed_max_score(client, db_session, show_with_car):
    """HB2 review fix: a handheld must never derive Max Score itself —
    Home Base computes it (active categories x score_range_max) and
    sends it directly. See PROTOCOL.md's configuration example."""
    show, category, car = show_with_car  # one active category ("Engine"), default score_range_max=5

    result = _submit(client, "hh-1", "001", closed_at_uptime_ms=1000, category_id=category.id, points=3)

    configuration = result["configuration"]
    assert configuration is not None
    assert configuration["score_range_max"] == 5
    assert configuration["max_score"] == 5  # 1 active category x 5
