"""
Post-creation Judging Category management — see CONTEXT.md's Edit Show
"Judging Setup" section. The protection rules themselves are tested in
test_judging_category_rules.py; this file tests that the management
layer actually enforces them (and that everything else — rename,
reorder, Overall Impression — stays unguarded, per CONTEXT.md) and
bumps configuration_revision correctly.
"""
from datetime import date, datetime, timezone

import pytest

from app.models import Car, CarStatus, Handheld, JudgingCategory, JudgingScore, JudgingSubmission, Show, SubmissionStatus
from app.services.judging_categories import (
    reorder_categories,
    rename_category,
    toggle_category,
    toggle_overall_impression,
)


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


def _judge_car(db, show, handheld, category):
    car = Car(show_id=show.id, entry_number="001", status=CarStatus.JUDGED, data_revision_at_change=1)
    db.add(car)
    db.flush()
    submission = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number="001", handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=0, status=SubmissionStatus.ACCEPTED,
    )
    db.add(submission)
    db.flush()
    db.add(JudgingScore(submission_id=submission.id, category_id=category.id, original_points=4, original_range_max=5, adjusted_points=4))
    db.commit()


def test_toggle_off_allowed_when_no_scores(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    revision_before = show.configuration_revision

    result = toggle_category(db_session, show, engine)

    assert result is None
    assert engine.active is False
    assert show.configuration_revision == revision_before + 1


def test_toggle_off_blocked_once_scored(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    _judge_car(db_session, show, handheld, engine)
    revision_before = show.configuration_revision

    result = toggle_category(db_session, show, engine)

    assert result is not None
    assert "Engine" in result
    assert engine.active is True  # unchanged
    assert show.configuration_revision == revision_before  # not bumped — nothing happened


def test_toggle_on_blocked_once_any_car_is_judged(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    _judge_car(db_session, show, handheld, engine)  # Paint itself has no scores

    result = toggle_category(db_session, show, paint)

    assert result is not None
    assert "Paint" in result
    assert paint.active is False  # unchanged


def test_toggle_on_allowed_before_any_car_is_judged(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories

    result = toggle_category(db_session, show, paint)

    assert result is None
    assert paint.active is True


def test_rename_always_allowed_even_after_judging(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    _judge_car(db_session, show, handheld, engine)
    revision_before = show.configuration_revision

    rename_category(db_session, show, engine, "Motor")

    assert engine.name == "Motor"
    assert show.configuration_revision == revision_before + 1


def test_reorder_always_allowed_even_after_judging(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    _judge_car(db_session, show, handheld, engine)
    revision_before = show.configuration_revision

    reorder_categories(db_session, show, [paint.id, engine.id])

    assert paint.sort_order == 0
    assert engine.sort_order == 1
    assert show.configuration_revision == revision_before + 1


def test_toggle_overall_impression_bumps_configuration_revision(db_session, show_with_categories):
    show, engine, paint, handheld = show_with_categories
    assert show.overall_impression_enabled is False
    revision_before = show.configuration_revision

    toggle_overall_impression(db_session, show)

    assert show.overall_impression_enabled is True
    assert show.configuration_revision == revision_before + 1
