"""
Both directions of CONTEXT.md's "Protecting completed work" rule for
Judging Categories — see services/judging_category_rules.py and
services/scoring.py::build_ranking_key. Fix from the HB2 review: the
old code only blocked turning a category OFF; nothing stopped turning
one ON after cars were already judged without it, and build_ranking_key
silently defaulted a missing score to 0 instead of surfacing the
problem.
"""
from datetime import date, datetime, timezone

import pytest
from sqlalchemy import select

from app.models import Car, CarStatus, Handheld, JudgingCategory, JudgingScore, JudgingSubmission, Show, SubmissionStatus
from app.services.judging_category_rules import can_activate_category, can_deactivate_category
from app.services.scoring import MissingCategoryScoreError, build_ranking_key


@pytest.fixture()
def show_with_categories(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field", score_range_max=5)
    db_session.add(show)
    db_session.commit()
    engine = JudgingCategory(show_id=show.id, name="Engine", sort_order=0, active=True)
    paint = JudgingCategory(show_id=show.id, name="Paint", sort_order=1, active=False)
    handheld = Handheld(label="hh-1")
    db_session.add_all([engine, paint, handheld])
    db_session.commit()
    return show, engine, paint, handheld


def _judge_car(db, show, handheld, entry_number, category, points):
    car = Car(show_id=show.id, entry_number=entry_number, status=CarStatus.JUDGED, data_revision_at_change=1)
    db.add(car)
    db.flush()
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        entry_number=entry_number,
        handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc),
        closed_at_uptime_ms=0,
        status=SubmissionStatus.ACCEPTED,
    )
    db.add(submission)
    db.flush()
    db.add(
        JudgingScore(
            submission_id=submission.id, category_id=category.id, original_points=points, original_range_max=5, adjusted_points=points
        )
    )
    db.commit()
    return car, submission


# ---------------------------------------------------------------------
# Deactivation guard (pre-existing direction — verifying it still works)
# ---------------------------------------------------------------------

def test_can_deactivate_with_no_scores_is_allowed(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    assert can_deactivate_category(db_session, paint) is None


def test_can_deactivate_blocked_once_category_has_scores(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    _judge_car(db_session, show, handheld, "001", engine, 4)

    reason = can_deactivate_category(db_session, engine)
    assert reason is not None
    assert "Engine" in reason
    assert "already been judged" in reason


# ---------------------------------------------------------------------
# Activation guard (the fix)
# ---------------------------------------------------------------------

def test_can_activate_before_any_car_is_judged_is_allowed(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    assert can_activate_category(db_session, paint) is None


def test_can_activate_blocked_once_any_car_is_judged(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    # A car judged on Engine only (Paint is off) — Paint itself has zero
    # scores, but the show has judged cars, which is what must block it.
    _judge_car(db_session, show, handheld, "001", engine, 4)

    reason = can_activate_category(db_session, paint)
    assert reason is not None
    assert "Paint" in reason
    assert reason == (
        "Cars have already been judged without Paint. Adding it now would "
        "leave those cars unscored in that category."
    )


def test_can_activate_blocked_even_for_a_category_with_no_scores_of_its_own(db_session, show_with_categories):
    """The important case: Paint itself has never been scored (it was
    off), but that's exactly the problem — blocking must key off "has the
    SHOW judged any cars", not "does THIS category have scores"."""
    show, engine, paint, handheld = show_with_categories
    _judge_car(db_session, show, handheld, "001", engine, 4)
    assert db_session.scalar(select(JudgingScore).where(JudgingScore.category_id == paint.id)) is None
    assert can_activate_category(db_session, paint) is not None


# ---------------------------------------------------------------------
# build_ranking_key surfaces a missing score instead of defaulting to 0
# ---------------------------------------------------------------------

def test_build_ranking_key_raises_on_missing_active_category_score(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    _, submission = _judge_car(db_session, show, handheld, "001", engine, 4)

    # Simulate the guard having been bypassed: Paint is now active even
    # though this submission has no Paint score.
    paint.active = True
    db_session.commit()

    with pytest.raises(MissingCategoryScoreError, match="Paint"):
        build_ranking_key(show, submission, [engine, paint])


def test_build_ranking_key_succeeds_when_every_active_category_has_a_score(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    car = Car(show_id=show.id, entry_number="002", status=CarStatus.JUDGED, data_revision_at_change=1)
    db_session.add(car)
    db_session.flush()
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        entry_number="002",
        handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc),
        closed_at_uptime_ms=0,
        status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.flush()
    db_session.add(JudgingScore(submission_id=submission.id, category_id=engine.id, original_points=4, original_range_max=5, adjusted_points=4))
    db_session.add(JudgingScore(submission_id=submission.id, category_id=paint.id, original_points=3, original_range_max=5, adjusted_points=3))
    db_session.commit()

    key = build_ranking_key(show, submission, [engine, paint])
    assert key == (7, 4, 3)  # total, then engine, then paint
