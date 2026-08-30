from datetime import date, datetime, timezone

from app.models import Handheld, JudgingScore, JudgingSubmission, SubmissionStatus
from app.services import car_classes as car_classes_service
from app.services import cars as cars_service
from app.services import criteria as criteria_service
from app.services import scoring
from app.services import shows as shows_service


def _make_show(db_session):
    return shows_service.create_show(db_session, "Fall Cruise-In", date(2026, 9, 12), "Downtown Lot")


def _make_car(db_session, show, reg, class_id=None):
    return cars_service.create_car(db_session, show.id, reg, reg, "Chevrolet", "Camaro", 1967, class_id)


def _make_handheld(db_session, label="hh-1"):
    handheld = Handheld(label=label)
    db_session.add(handheld)
    db_session.commit()
    return handheld


def _accept_submission(db_session, show, car, handheld, scores: dict, judge_name="Judge"):
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        judge_name=judge_name,
        closed_at=datetime.now(timezone.utc),
        status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.commit()
    for criteria, points in scores.items():
        db_session.add(JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=points))
    car.status = car.status  # no-op, keeps intent explicit
    db_session.commit()
    return submission


def test_unscored_car_never_appears_in_rankings(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    handheld = _make_handheld(db_session)

    scored_car = _make_car(db_session, show, "0142")
    _accept_submission(db_session, show, scored_car, handheld, {paint: 20})

    unscored_car = _make_car(db_session, show, "0231")  # never judged

    rankings = scoring.overall_rankings(db_session, show.id)

    reg_numbers = [entry.car.registration_number for entry in rankings]
    assert "0142" in reg_numbers
    assert "0231" not in reg_numbers  # never appears as a phantom 0-point last place
    assert len(rankings) == 1


def test_total_score_is_sum_of_accepted_submission_scores(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    engine = criteria_service.create_criteria(db_session, show.id, "Engine", 25)
    handheld = _make_handheld(db_session)

    car = _make_car(db_session, show, "0142")
    _accept_submission(db_session, show, car, handheld, {paint: 20, engine: 18})

    rankings = scoring.overall_rankings(db_session, show.id)

    assert len(rankings) == 1
    assert rankings[0].score == 38


def test_rejected_submission_scores_are_excluded_from_total(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    handheld = _make_handheld(db_session)
    car = _make_car(db_session, show, "0142")

    accepted = _accept_submission(db_session, show, car, handheld, {paint: 20})
    # a rejected competing submission with a much higher score must NOT count
    rejected = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc),
        status=SubmissionStatus.REJECTED,
    )
    db_session.add(rejected)
    db_session.commit()
    db_session.add(JudgingScore(submission_id=rejected.id, criteria_id=paint.id, points=25))
    db_session.commit()

    rankings = scoring.overall_rankings(db_session, show.id)
    assert rankings[0].score == 20


def test_ties_are_flagged_with_shared_competition_rank(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    handheld = _make_handheld(db_session)

    car_a = _make_car(db_session, show, "0001")
    car_b = _make_car(db_session, show, "0002")
    car_c = _make_car(db_session, show, "0003")
    _accept_submission(db_session, show, car_a, handheld, {paint: 25})
    _accept_submission(db_session, show, car_b, handheld, {paint: 25})  # tied with A
    _accept_submission(db_session, show, car_c, handheld, {paint: 20})

    rankings = scoring.overall_rankings(db_session, show.id)
    by_reg = {r.car.registration_number: r for r in rankings}

    assert by_reg["0001"].rank == 1
    assert by_reg["0002"].rank == 1
    assert by_reg["0001"].tied is True
    assert by_reg["0002"].tied is True
    # next distinct score jumps to position 3, not rank 2 ("1224" ranking)
    assert by_reg["0003"].rank == 3
    assert by_reg["0003"].tied is False


def test_class_rankings_groups_by_class_and_includes_unclassified(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    handheld = _make_handheld(db_session)
    muscle = car_classes_service.create_class(db_session, show.id, "1970s Muscle")

    classed_car = _make_car(db_session, show, "0142", class_id=muscle.id)
    unclassed_car = _make_car(db_session, show, "0231")
    _accept_submission(db_session, show, classed_car, handheld, {paint: 20})
    _accept_submission(db_session, show, unclassed_car, handheld, {paint: 15})

    groups = scoring.class_rankings(db_session, show.id)
    groups_by_label = {(car_class.name if car_class else None): entries for car_class, entries in groups}

    assert "1970s Muscle" in groups_by_label
    assert groups_by_label["1970s Muscle"][0].car.registration_number == "0142"
    assert None in groups_by_label  # Unclassified group present
    assert groups_by_label[None][0].car.registration_number == "0231"


def test_criteria_leaderboard_only_includes_cars_scored_on_that_criterion(db_session):
    show = _make_show(db_session)
    paint = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    engine = criteria_service.create_criteria(db_session, show.id, "Engine", 25)
    handheld = _make_handheld(db_session)

    car_a = _make_car(db_session, show, "0001")
    car_b = _make_car(db_session, show, "0002")
    _accept_submission(db_session, show, car_a, handheld, {paint: 22, engine: 20})
    _accept_submission(db_session, show, car_b, handheld, {paint: 18})  # never scored on Engine

    boards = scoring.criteria_leaderboards(db_session, show.id)
    boards_by_name = {criteria.name: entries for criteria, entries in boards}

    assert len(boards_by_name["Paint"]) == 2
    assert len(boards_by_name["Engine"]) == 1
    assert boards_by_name["Engine"][0].car.registration_number == "0001"
