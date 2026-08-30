import time
from datetime import date, datetime, timezone

from app.models import CarStatus, Handheld, JudgingSubmission, SubmissionStatus
from app.services import car_classes as car_classes_service
from app.services import cars as cars_service
from app.services import criteria as criteria_service
from app.services import shows as shows_service


def _make_show(db_session):
    return shows_service.create_show(db_session, "Fall Cruise-In", date(2026, 9, 12), "Downtown Lot")


def _make_car(db_session, show, reg="0142", **overrides):
    kwargs = dict(
        show_id=show.id,
        registration_number=reg,
        display_car_number=reg,
        make="Chevrolet",
        model="Camaro",
        year=1967,
        class_id=None,
    )
    kwargs.update(overrides)
    return cars_service.create_car(db_session, **kwargs)


def _make_criteria(db_session, show, name="Paint", max_points=25):
    return criteria_service.create_criteria(db_session, show.id, name, max_points)


# ---------------------------------------------------------------------------
# GET /sync/roster
# ---------------------------------------------------------------------------


def test_first_sync_returns_full_roster(client, db_session):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    _make_car(db_session, show, "0231")
    _make_criteria(db_session, show, "Paint")

    resp = client.get("/api/v1/sync/roster", params={"handheld_id": "hh-1"})

    assert resp.status_code == 200
    body = resp.json()
    assert len(body["cars"]) == 2
    assert len(body["criteria"]) == 1
    assert body["summary"]["total_cars"] == 2
    assert body["summary"]["unjudged"] == 2
    assert "server_time" in body

    handheld = db_session.query(Handheld).filter_by(label="hh-1").first()
    assert handheld is not None
    assert handheld.last_sync_at is not None
    assert handheld.last_ip is not None


def test_delta_sync_returns_only_updated_cars(client, db_session):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")

    cutoff = datetime.now(timezone.utc)
    time.sleep(0.02)

    _make_car(db_session, show, "0231")

    resp = client.get(
        "/api/v1/sync/roster", params={"handheld_id": "hh-1", "since": cutoff.isoformat()}
    )

    assert resp.status_code == 200
    body = resp.json()
    regs = [c["registration_number"] for c in body["cars"]]
    assert regs == ["0231"]
    # summary is always full-roster, regardless of the delta filter
    assert body["summary"]["total_cars"] == 2


# ---------------------------------------------------------------------------
# POST /sync/submissions
# ---------------------------------------------------------------------------


def test_empty_submissions_is_a_valid_checkin(client, db_session):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")

    resp = client.post("/api/v1/sync/submissions", json={"handheld_id": "hh-1", "submissions": []})

    assert resp.status_code == 200
    body = resp.json()
    assert body["results"] == []
    assert body["summary"]["total_cars"] == 1
    assert "roster_delta" in body


def test_duplicate_from_different_handheld_flags_conflict(client, db_session):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    _make_criteria(db_session, show, "Paint", 25)

    first = client.post(
        "/api/v1/sync/submissions",
        json={
            "handheld_id": "hh-1",
            "submissions": [
                {
                    "registration_number": "0142",
                    "closed_at": "2026-09-12T14:00:00Z",
                    "scores": [{"criteria_name": "Paint", "points": 20}],
                }
            ],
        },
    )
    assert first.json()["results"][0]["status"] == "accepted"

    second = client.post(
        "/api/v1/sync/submissions",
        json={
            "handheld_id": "hh-2",
            "submissions": [
                {
                    "registration_number": "0142",
                    "closed_at": "2026-09-12T14:05:00Z",  # different judge, different time
                    "scores": [{"criteria_name": "Paint", "points": 18}],
                }
            ],
        },
    )
    assert second.status_code == 200
    result = second.json()["results"][0]
    assert result["status"] == "flagged_duplicate"

    car = cars_service.list_cars(db_session, show.id)[0]
    db_session.refresh(car)
    assert car.status == CarStatus.FLAGGED_CONFLICT

    # original accepted submission must be untouched
    accepted = (
        db_session.query(JudgingSubmission)
        .filter_by(registration_number="0142", status=SubmissionStatus.ACCEPTED)
        .one()
    )
    assert accepted.handheld.label == "hh-1"


def test_same_submission_retry_from_same_handheld_is_not_flagged(client, db_session):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    _make_criteria(db_session, show, "Paint", 25)

    payload = {
        "handheld_id": "hh-1",
        "submissions": [
            {
                "registration_number": "0142",
                "closed_at": "2026-09-12T14:00:00Z",
                "scores": [{"criteria_name": "Paint", "points": 20}],
            }
        ],
    }

    first = client.post("/api/v1/sync/submissions", json=payload)
    assert first.json()["results"][0]["status"] == "accepted"

    # Simulate a lost ack: the handheld resends the identical submission.
    second = client.post("/api/v1/sync/submissions", json=payload)
    assert second.status_code == 200
    result = second.json()["results"][0]
    assert result["status"] == "accepted"
    assert result["status"] != "flagged_duplicate"

    car = cars_service.list_cars(db_session, show.id)[0]
    db_session.refresh(car)
    assert car.status == CarStatus.JUDGED  # never flipped to conflict

    all_submissions = db_session.query(JudgingSubmission).filter_by(registration_number="0142").all()
    assert len(all_submissions) == 1  # no duplicate row created for the retry


def test_unknown_criteria_name_is_a_per_item_error(client, db_session):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    _make_criteria(db_session, show, "Paint", 25)

    resp = client.post(
        "/api/v1/sync/submissions",
        json={
            "handheld_id": "hh-1",
            "submissions": [
                {
                    "registration_number": "0142",
                    "closed_at": "2026-09-12T14:00:00Z",
                    "scores": [{"criteria_name": "Upholstery", "points": 10}],
                }
            ],
        },
    )

    assert resp.status_code == 200
    result = resp.json()["results"][0]
    assert result["status"] == "error"
    assert "Upholstery" in result["message"]

    car = cars_service.list_cars(db_session, show.id)[0]
    db_session.refresh(car)
    assert car.status == CarStatus.UNJUDGED  # nothing was recorded
    assert db_session.query(JudgingSubmission).filter_by(registration_number="0142").count() == 0


def test_unknown_registration_number_is_held_for_reconciliation(client, db_session):
    show = _make_show(db_session)
    _make_criteria(db_session, show, "Paint", 25)
    # deliberately no car "9999" in the roster

    resp = client.post(
        "/api/v1/sync/submissions",
        json={
            "handheld_id": "hh-1",
            "submissions": [
                {
                    "registration_number": "9999",
                    "closed_at": "2026-09-12T14:00:00Z",
                    "scores": [{"criteria_name": "Paint", "points": 20}],
                }
            ],
        },
    )

    assert resp.status_code == 200
    result = resp.json()["results"][0]
    # Wire protocol only defines accepted/flagged_duplicate/error — an
    # unmatched registration number is still "accepted" at the wire level;
    # see DECISIONS.md.
    assert result["status"] == "accepted"

    held = db_session.query(JudgingSubmission).filter_by(registration_number="9999").one()
    assert held.car_id is None
    assert held.status == SubmissionStatus.UNMATCHED
    assert len(held.scores) == 1


def test_registering_the_car_reconciles_a_prior_unmatched_submission(client, db_session):
    show = _make_show(db_session)
    _make_criteria(db_session, show, "Paint", 25)

    client.post(
        "/api/v1/sync/submissions",
        json={
            "handheld_id": "hh-1",
            "submissions": [
                {
                    "registration_number": "9999",
                    "closed_at": "2026-09-12T14:00:00Z",
                    "scores": [{"criteria_name": "Paint", "points": 20}],
                }
            ],
        },
    )
    held = db_session.query(JudgingSubmission).filter_by(registration_number="9999").one()
    assert held.status == SubmissionStatus.UNMATCHED

    # The host adds the late-registered car after the fact.
    car = _make_car(db_session, show, "9999")

    db_session.refresh(held)
    assert held.status == SubmissionStatus.ACCEPTED
    assert held.car_id == car.id
    db_session.refresh(car)
    assert car.status == CarStatus.JUDGED
