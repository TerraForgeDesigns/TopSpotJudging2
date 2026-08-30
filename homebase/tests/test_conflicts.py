"""
Conflict resolution — see CONTEXT.md and services/conflicts.py.
"""
from datetime import date, datetime, timezone

import pytest

from app.models import Car, CarStatus, Handheld, JudgingCategory, JudgingScore, JudgingSubmission, Show, SubmissionStatus
from app.services.conflicts import accept_submission, build_conflict_view, create_corrected_submission, list_conflicted_cars


@pytest.fixture()
def conflict_setup(db_session):
    show = Show(name="Show", event_date=date(2026, 9, 1), score_range_max=5)
    db_session.add(show)
    db_session.commit()
    engine = JudgingCategory(show_id=show.id, name="Engine", built_in_key="engine", sort_order=0, active=True)
    hh1 = Handheld(label="hh-1")
    hh2 = Handheld(label="hh-2")
    db_session.add_all([engine, hh1, hh2])
    db_session.commit()

    car = Car(show_id=show.id, entry_number="148", status=CarStatus.FLAGGED_CONFLICT, data_revision_at_change=1)
    db_session.add(car)
    db_session.flush()

    sub1 = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number="148", handheld_id=hh1.id, judge_name="R. Alvarez",
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=100, status=SubmissionStatus.ACCEPTED,
    )
    sub2 = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number="148", handheld_id=hh2.id, judge_name="T. Nguyen",
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=200, status=SubmissionStatus.FLAGGED_DUPLICATE,
    )
    db_session.add_all([sub1, sub2])
    db_session.flush()
    db_session.add(JudgingScore(submission_id=sub1.id, category_id=engine.id, original_points=4, original_range_max=5, adjusted_points=4))
    db_session.add(JudgingScore(submission_id=sub2.id, category_id=engine.id, original_points=5, original_range_max=5, adjusted_points=5))
    db_session.commit()
    return show, car, sub1, sub2, engine


def test_list_conflicted_cars_finds_the_flagged_car(db_session, conflict_setup):
    show, car, sub1, sub2, engine = conflict_setup
    conflicted = list_conflicted_cars(db_session, show.id)
    assert [c.id for c in conflicted] == [car.id]


def test_build_conflict_view_shows_both_submissions_side_by_side(db_session, conflict_setup):
    show, car, sub1, sub2, engine = conflict_setup
    view = build_conflict_view(car)
    assert len(view.rows) == 2
    totals = {row.submission.id: row.total for row in view.rows}
    assert totals[sub1.id] == 4
    assert totals[sub2.id] == 5


def test_accept_submission_rejects_the_other_and_judges_the_car(db_session, conflict_setup):
    show, car, sub1, sub2, engine = conflict_setup
    revision_before = show.show_data_revision

    accept_submission(db_session, show, car, sub2.id)

    db_session.refresh(sub1)
    db_session.refresh(sub2)
    db_session.refresh(car)
    assert sub2.status == SubmissionStatus.ACCEPTED
    assert sub1.status == SubmissionStatus.REJECTED  # kept, not deleted
    assert car.status == CarStatus.JUDGED
    assert show.show_data_revision == revision_before + 1
    assert car.data_revision_at_change == show.show_data_revision


def test_create_corrected_submission_rejects_both_originals(db_session, conflict_setup):
    show, car, sub1, sub2, engine = conflict_setup

    corrected = create_corrected_submission(db_session, show, car, {engine.id: 3}, note="Re-checked the paper sheet")

    db_session.refresh(sub1)
    db_session.refresh(sub2)
    db_session.refresh(car)
    assert sub1.status == SubmissionStatus.REJECTED
    assert sub2.status == SubmissionStatus.REJECTED
    assert corrected.status == SubmissionStatus.ACCEPTED
    assert corrected.note == "Re-checked the paper sheet"
    assert corrected.scores[0].adjusted_points == 3
    assert car.status == CarStatus.JUDGED
