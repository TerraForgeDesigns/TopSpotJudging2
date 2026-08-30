from datetime import date, datetime, timezone

from app.models import AwardCategory, Handheld, JudgingScore, JudgingSubmission, SubmissionStatus
from app.services import awards as awards_service
from app.services import car_classes as car_classes_service
from app.services import cars as cars_service
from app.services import criteria as criteria_service
from app.services import shows as shows_service


def _make_show(db_session):
    return shows_service.create_show(db_session, "Fall Cruise-In", date(2026, 9, 12), "Downtown Lot")


def _make_car(db_session, show, reg, class_id=None):
    return cars_service.create_car(db_session, show.id, reg, reg, "Chevrolet", "Camaro", 1967, class_id)


def _score(db_session, show, car, points_by_criteria: dict, label):
    handheld = Handheld(label=label)
    db_session.add(handheld)
    db_session.commit()
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc),
        status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.commit()
    for criteria, points in points_by_criteria.items():
        db_session.add(JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=points))
    db_session.commit()


def test_overall_award_suggests_rank_one(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    winner = _make_car(db_session, show, "0001")
    runner_up = _make_car(db_session, show, "0002")
    _score(db_session, show, winner, {paint: 25}, "hh-1")
    _score(db_session, show, runner_up, {paint: 20}, "hh-2")

    award = awards_service.create_award(db_session, show.id, "Best in Show", AwardCategory.OVERALL)
    suggestion = awards_service.suggest_winner(db_session, award)

    assert suggestion.ambiguous is False
    assert suggestion.entry.car.registration_number == "0001"


def test_tied_rank_one_is_ambiguous_not_auto_picked(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    a = _make_car(db_session, show, "0001")
    b = _make_car(db_session, show, "0002")
    _score(db_session, show, a, {paint: 25}, "hh-1")
    _score(db_session, show, b, {paint: 25}, "hh-2")

    award = awards_service.create_award(db_session, show.id, "Best in Show", AwardCategory.OVERALL)
    suggestion = awards_service.suggest_winner(db_session, award)

    assert suggestion.ambiguous is True
    assert suggestion.entry is None


def test_criteria_award_suggests_best_on_that_criterion_only(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    engine = criteria_service.create_criteria(db_session, show.id, "Engine", 25)
    paint_winner = _make_car(db_session, show, "0001")
    engine_winner = _make_car(db_session, show, "0002")
    _score(db_session, show, paint_winner, {paint: 25, engine: 10}, "hh-1")
    _score(db_session, show, engine_winner, {paint: 15, engine: 25}, "hh-2")

    award = awards_service.create_award(db_session, show.id, "Best Paint", AwardCategory.CRITERIA, criteria_id=paint.id)
    suggestion = awards_service.suggest_winner(db_session, award)

    assert suggestion.entry.car.registration_number == "0001"


def test_class_award_suggests_best_within_class_only(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    trucks = car_classes_service.create_class(db_session, show.id, "Trucks")
    truck_car = _make_car(db_session, show, "0001", class_id=trucks.id)
    other_car = _make_car(db_session, show, "0002")  # unclassified, higher score
    _score(db_session, show, truck_car, {paint: 15}, "hh-1")
    _score(db_session, show, other_car, {paint: 25}, "hh-2")

    award = awards_service.create_award(db_session, show.id, "Best in Class: Trucks", AwardCategory.CLASS, class_id=trucks.id)
    suggestion = awards_service.suggest_winner(db_session, award)

    assert suggestion.entry.car.registration_number == "0001"


def test_host_override_persists_over_suggestion(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    winner = _make_car(db_session, show, "0001")
    override_target = _make_car(db_session, show, "0002")
    _score(db_session, show, winner, {paint: 25}, "hh-1")
    _score(db_session, show, override_target, {paint: 10}, "hh-2")

    award = awards_service.create_award(db_session, show.id, "Best in Show", AwardCategory.OVERALL)
    awards_service.set_winner(db_session, award.id, override_target.id)

    reloaded = awards_service.get_award(db_session, award.id)
    assert reloaded.winner_car_id == override_target.id


def test_suggestion_is_none_when_nothing_scored_yet(db_session):
    show = _make_show(db_session)
    criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    _make_car(db_session, show, "0001")  # never judged

    award = awards_service.create_award(db_session, show.id, "Best in Show", AwardCategory.OVERALL)
    suggestion = awards_service.suggest_winner(db_session, award)

    assert suggestion.entry is None
    assert suggestion.ambiguous is False
