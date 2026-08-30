"""
Range escalation — see CONTEXT.md's "Judging categories & scoring range"
and "Score conversion" sections. Scenario per this task's spec: 140 cars
judged at 1-5, add 20 more (crossing the 151-car threshold), escalate,
and verify conversion + revision-bump + idempotency + the never-lowers
rule all hold.

services/range_escalation.py does not exist yet as of this commit —
these tests are written against the INTENDED behavior first (see this
task's execution order) and are expected to fail until Step 5.
"""
from datetime import date, datetime, timezone

import pytest
from sqlalchemy import select

from app.models import Car, CarStatus, Handheld, JudgingCategory, JudgingScore, JudgingSubmission, Show, SubmissionStatus
from app.services.range_escalation import check_and_escalate, range_max_for_car_count
from app.services.score_conversion import convert_score


def _add_judged_car(db, show, category, handheld, entry_number, points, range_max):
    car = Car(
        show_id=show.id,
        entry_number=entry_number,
        status=CarStatus.JUDGED,
        data_revision_at_change=show.show_data_revision,
    )
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
    score = JudgingScore(
        submission_id=submission.id,
        category_id=category.id,
        original_points=points,
        original_range_max=range_max,
        adjusted_points=points,  # not yet escalated — adjusted starts equal to original
    )
    db.add(score)
    db.commit()
    return car


def _add_unjudged_car(db, show, entry_number):
    car = Car(show_id=show.id, entry_number=entry_number, data_revision_at_change=show.show_data_revision)
    db.add(car)
    db.commit()
    return car


def test_range_max_for_car_count_tiers():
    assert range_max_for_car_count(1) == 5
    assert range_max_for_car_count(150) == 5
    assert range_max_for_car_count(151) == 10
    assert range_max_for_car_count(300) == 10
    assert range_max_for_car_count(301) == 25
    assert range_max_for_car_count(500) == 25
    assert range_max_for_car_count(5000) == 25  # open-ended — stays at 25


def test_escalation_full_scenario(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field", score_range_max=5)
    db_session.add(show)
    db_session.commit()
    category = JudgingCategory(show_id=show.id, name="Engine", sort_order=1, active=True)
    db_session.add(category)
    handheld = Handheld(label="hh-1")
    db_session.add(handheld)
    db_session.commit()

    starting_config_revision = show.configuration_revision

    # 140 cars judged at 1-5, varying points so relative order is meaningful.
    cars = []
    original_points_by_entry = {}
    for i in range(140):
        entry_number = f"{i + 1:03d}"
        points = (i % 5) + 1  # cycles 1..5
        car = _add_judged_car(db_session, show, category, handheld, entry_number, points, 5)
        cars.append(car)
        original_points_by_entry[entry_number] = points

    # Add 20 more cars (unjudged is fine — the threshold is car COUNT, not
    # judged count, per CONTEXT.md) to cross 150 -> the 151-300 tier.
    for i in range(140, 160):
        _add_unjudged_car(db_session, show, f"{i + 1:03d}")

    assert db_session.scalar(select(Car).where(Car.show_id == show.id)) is not None
    car_count = len(db_session.scalars(select(Car).where(Car.show_id == show.id)).all())
    assert car_count == 160

    escalated = check_and_escalate(db_session, show)
    db_session.commit()  # check_and_escalate no longer commits — the caller does, see DECISIONS.md
    assert escalated is True
    assert show.score_range_max == 10
    assert show.configuration_revision == starting_config_revision + 1

    scores = list(
        db_session.scalars(
            select(JudgingScore).join(JudgingSubmission).where(JudgingSubmission.show_id == show.id)
        )
    )
    assert len(scores) == 140
    for score in scores:
        # Original is NEVER touched.
        assert score.original_range_max == 5
        original = score.original_points
        # Adjusted is recomputed from the original via the shared conversion fn.
        assert score.adjusted_points == convert_score(original, 5, 10)

    # Relative ranking order preserved: a car that scored higher at 1-5
    # must still score higher (or equal) at 1-10 after conversion — the
    # doubling conversion (5 -> 10) is exact and order-preserving.
    by_entry = {s.submission.entry_number: s.adjusted_points for s in scores}
    for entry_a, points_a in original_points_by_entry.items():
        for entry_b, points_b in original_points_by_entry.items():
            if points_a > points_b:
                assert by_entry[entry_a] > by_entry[entry_b]
            elif points_a == points_b:
                assert by_entry[entry_a] == by_entry[entry_b]

    # Idempotent: calling again with no new cars changes nothing.
    revision_after_first_escalation = show.configuration_revision
    sample_score_id = scores[0].id
    sample_adjusted_before = scores[0].adjusted_points

    escalated_again = check_and_escalate(db_session, show)
    db_session.commit()
    assert escalated_again is False
    assert show.score_range_max == 10
    assert show.configuration_revision == revision_after_first_escalation

    db_session.refresh(db_session.get(JudgingScore, sample_score_id))
    unchanged_score = db_session.get(JudgingScore, sample_score_id)
    assert unchanged_score.adjusted_points == sample_adjusted_before

    # Never lowers: remove cars back under the 150 threshold and confirm
    # the range holds at 10, not back down to 5.
    surviving_cars = list(db_session.scalars(select(Car).where(Car.show_id == show.id)))
    for car in surviving_cars[:40]:  # 160 - 40 = 120, back under the 151 threshold
        db_session.delete(car)
    db_session.commit()

    remaining_count = len(db_session.scalars(select(Car).where(Car.show_id == show.id)).all())
    assert remaining_count == 120
    assert range_max_for_car_count(remaining_count) == 5  # the tier a fresh show at this count WOULD get

    escalated_after_removal = check_and_escalate(db_session, show)
    db_session.commit()
    assert escalated_after_removal is False
    assert show.score_range_max == 10  # held, not lowered back to 5


def test_failure_after_escalation_leaves_no_partial_state(db_session):
    """The regression this guards against: check_and_escalate() used to
    call db.commit() itself, which meant a caller composing it into a
    larger all-or-nothing operation (e.g. Add Cars: create entries, bump
    show_data_revision, escalate) could NOT roll the whole thing back —
    escalation's own commit would have already locked in the car-adds and
    the revision bump, silently, even if a later step in that same
    operation failed. See DECISIONS.md and services/revisions.py."""
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field", score_range_max=5)
    db_session.add(show)
    db_session.commit()

    car_count_before = 145
    for i in range(car_count_before):
        _add_unjudged_car(db_session, show, f"{i + 1:03d}")
    revision_before = show.configuration_revision
    range_before = show.score_range_max

    class SimulatedFailure(Exception):
        pass

    try:
        # Simulate "Add Cars" as one transaction: add entries that cross
        # the 150 threshold, bump the data revision, escalate — then
        # something later in that same logical operation fails, before
        # anything has been committed.
        for i in range(car_count_before, car_count_before + 10):  # 145 -> 155, crosses 150
            new_car = Car(show_id=show.id, entry_number=f"{i + 1:03d}", data_revision_at_change=show.show_data_revision)
            db_session.add(new_car)
        db_session.flush()
        from app.services.revisions import bump_show_data_revision

        bump_show_data_revision(db_session, show)
        escalated = check_and_escalate(db_session, show)
        assert escalated is True  # the escalation itself worked, uncommitted
        raise SimulatedFailure("something later in this Add Cars operation failed")
    except SimulatedFailure:
        db_session.rollback()

    # Nothing from the failed operation survives — not the new cars, not
    # the revision bump, not the escalation.
    db_session.expire_all()
    remaining = db_session.scalars(select(Car).where(Car.show_id == show.id)).all()
    assert len(remaining) == car_count_before
    refreshed_show = db_session.get(Show, show.id)
    assert refreshed_show.configuration_revision == revision_before
    assert refreshed_show.show_data_revision == 1
    assert refreshed_show.score_range_max == range_before
