from datetime import date, datetime, timezone

from app.models import CarStatus, Handheld, JudgingScore, JudgingSubmission, SubmissionStatus
from app.services import cars as cars_service
from app.services import conflicts as conflicts_service
from app.services import criteria as criteria_service
from app.services import shows as shows_service


def _make_show(db_session):
    return shows_service.create_show(db_session, "Fall Cruise-In", date(2026, 9, 12), "Downtown Lot")


def _make_car(db_session, show, reg="0142"):
    return cars_service.create_car(db_session, show.id, reg, reg, "Chevrolet", "Camaro", 1967, None)


def _make_handheld(db_session, label):
    handheld = Handheld(label=label)
    db_session.add(handheld)
    db_session.commit()
    return handheld


def _add_submission(db_session, show, car, handheld, status, scores: dict, judge_name="Judge", registration_number=None):
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id if car else None,
        registration_number=registration_number if car is None else car.registration_number,
        handheld_id=handheld.id,
        judge_name=judge_name,
        closed_at=datetime.now(timezone.utc),
        status=status,
    )
    db_session.add(submission)
    db_session.commit()
    for criteria, points in scores.items():
        db_session.add(JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=points))
    db_session.commit()
    return submission


def _make_conflicted_car(db_session, show, paint):
    car = _make_car(db_session, show, "0142")
    hh1 = _make_handheld(db_session, "hh-1")
    hh2 = _make_handheld(db_session, "hh-2")
    first = _add_submission(db_session, show, car, hh1, SubmissionStatus.ACCEPTED, {paint: 20}, "R. Alvarez")
    second = _add_submission(db_session, show, car, hh2, SubmissionStatus.FLAGGED_DUPLICATE, {paint: 18}, "J. Smith")
    car.status = CarStatus.FLAGGED_CONFLICT
    db_session.commit()
    return car, first, second


# ---------------------------------------------------------------------------
# accept_submission
# ---------------------------------------------------------------------------


def test_accept_submission_rejects_the_others_and_judges_the_car(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    car, first, second = _make_conflicted_car(db_session, show, paint)

    conflicts_service.accept_submission(db_session, car.id, second.id)

    db_session.refresh(car)
    db_session.refresh(first)
    db_session.refresh(second)
    assert car.status == CarStatus.JUDGED
    assert second.status == SubmissionStatus.ACCEPTED
    assert first.status == SubmissionStatus.REJECTED


def test_accept_submission_rejects_cross_car_mismatch(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    car, first, second = _make_conflicted_car(db_session, show, paint)
    other_car = _make_car(db_session, show, "9999")

    try:
        conflicts_service.accept_submission(db_session, other_car.id, first.id)
        assert False, "expected ValueError"
    except ValueError:
        pass


# ---------------------------------------------------------------------------
# create_corrected_submission
# ---------------------------------------------------------------------------


def test_corrected_submission_rejects_existing_and_becomes_accepted(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    engine = criteria_service.create_criteria(db_session, show.id, "Engine", 25)
    car, first, second = _make_conflicted_car(db_session, show, paint)

    corrected = conflicts_service.create_corrected_submission(
        db_session, car.id, {paint.id: 24, engine.id: 22}, note="Transcribed from the original judge sheets"
    )

    db_session.refresh(car)
    db_session.refresh(first)
    db_session.refresh(second)
    assert car.status == CarStatus.JUDGED
    assert corrected.status == SubmissionStatus.ACCEPTED
    assert corrected.note == "Transcribed from the original judge sheets"
    assert first.status == SubmissionStatus.REJECTED
    assert second.status == SubmissionStatus.REJECTED
    assert sum(s.points for s in corrected.scores) == 46


# ---------------------------------------------------------------------------
# unmatched submission assign / discard
# ---------------------------------------------------------------------------


def test_assign_unmatched_submission_to_car_with_no_accepted(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    hh = _make_handheld(db_session, "hh-1")
    car = _make_car(db_session, show, "9999")
    held = _add_submission(
        db_session, show, None, hh, SubmissionStatus.UNMATCHED, {}, registration_number="9999"
    )
    db_session.add(JudgingScore(submission_id=held.id, criteria_id=paint.id, points=19))
    db_session.commit()

    conflicts_service.assign_unmatched_submission(db_session, held.id, car.id)

    db_session.refresh(held)
    db_session.refresh(car)
    assert held.status == SubmissionStatus.ACCEPTED
    assert held.car_id == car.id
    assert car.status == CarStatus.JUDGED


def test_assign_unmatched_submission_to_car_with_existing_accepted_flags_conflict(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    hh1 = _make_handheld(db_session, "hh-1")
    hh2 = _make_handheld(db_session, "hh-2")
    car = _make_car(db_session, show, "0142")
    _add_submission(db_session, show, car, hh1, SubmissionStatus.ACCEPTED, {paint: 20})

    held = _add_submission(
        db_session, show, None, hh2, SubmissionStatus.UNMATCHED, {}, registration_number="0142"
    )

    conflicts_service.assign_unmatched_submission(db_session, held.id, car.id)

    db_session.refresh(held)
    db_session.refresh(car)
    assert held.status == SubmissionStatus.FLAGGED_DUPLICATE
    assert car.status == CarStatus.FLAGGED_CONFLICT


def test_discard_unmatched_submission(db_session):
    show = _make_show(db_session)
    hh = _make_handheld(db_session, "hh-1")
    held = _add_submission(
        db_session, show, None, hh, SubmissionStatus.UNMATCHED, {}, registration_number="1234"
    )

    conflicts_service.discard_unmatched_submission(db_session, held.id)

    db_session.refresh(held)
    assert held.status == SubmissionStatus.REJECTED


# ---------------------------------------------------------------------------
# build_conflict_view
# ---------------------------------------------------------------------------


def test_build_conflict_view_pivots_criteria_as_rows(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    engine = criteria_service.create_criteria(db_session, show.id, "Engine", 25)
    car = _make_car(db_session, show, "0142")
    hh1 = _make_handheld(db_session, "hh-1")
    hh2 = _make_handheld(db_session, "hh-2")
    first = _add_submission(db_session, show, car, hh1, SubmissionStatus.ACCEPTED, {paint: 20, engine: 18})
    second = _add_submission(db_session, show, car, hh2, SubmissionStatus.FLAGGED_DUPLICATE, {paint: 22})

    db_session.refresh(car)
    view = conflicts_service.build_conflict_view(car)

    assert [c.name for c in view.criteria] == ["Paint", "Engine"]
    assert len(view.rows) == 2
    row_by_submission_id = {row.submission.id: row for row in view.rows}
    assert row_by_submission_id[first.id].total == 38
    assert row_by_submission_id[second.id].total == 22
    assert row_by_submission_id[second.id].scores_by_criteria_id.get(engine.id) is None
