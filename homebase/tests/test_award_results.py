"""
services/award_results.py — nomination-based winner resolution, the
high-scorers-not-nominated callout, the nominations-per-judge report,
Choose Winner, and mark-not-presented. See CONTEXT.md's Awards section.
"""
from datetime import date, datetime, timezone

import pytest

from app.models import (
    Award,
    AwardNomination,
    Car,
    CarStatus,
    Handheld,
    JudgingCategory,
    JudgingScore,
    JudgingSubmission,
    Show,
    SubmissionStatus,
)
from app.models.enums import AwardRankingBasis
from app.services.award_results import (
    award_rankings,
    choose_winner,
    high_scorers_not_nominated,
    mark_not_presented,
    nominations_per_judge_report,
    set_announcer_name,
    suggest_award_winner,
    use_suggested_winner,
)


@pytest.fixture()
def paint_award_setup(db_session):
    show = Show(name="Show", event_date=date(2026, 9, 1), score_range_max=25)
    db_session.add(show)
    db_session.commit()
    paint = JudgingCategory(show_id=show.id, name="Paint", built_in_key="paint", sort_order=0, active=True)
    hh1 = Handheld(label="hh-1")
    hh2 = Handheld(label="hh-2")
    db_session.add_all([paint, hh1, hh2])
    db_session.commit()
    award = Award(show_id=show.id, name="Best Paint", judge_chosen=True, ranking_basis=AwardRankingBasis.PAINT, sort_order=0)
    db_session.add(award)
    db_session.commit()
    return show, paint, hh1, hh2, award


def _judge_and_maybe_nominate(db, show, handheld, car, paint_points, award=None, closed_at_uptime_ms=0):
    submission = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number=car.entry_number, handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=closed_at_uptime_ms, status=SubmissionStatus.ACCEPTED,
    )
    db.add(submission)
    db.flush()
    paint = db.query(JudgingCategory).filter_by(show_id=show.id, built_in_key="paint").first()
    db.add(JudgingScore(submission_id=submission.id, category_id=paint.id, original_points=paint_points, original_range_max=show.score_range_max, adjusted_points=paint_points))
    if award is not None:
        db.add(AwardNomination(submission_id=submission.id, award_id=award.id))
    car.status = CarStatus.JUDGED
    db.commit()
    return submission


def _car(db, show, entry_number):
    car = Car(show_id=show.id, entry_number=entry_number, data_revision_at_change=1)
    db.add(car)
    db.commit()
    return car


def test_award_rankings_only_includes_nominated_cars(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    nominated = _car(db_session, show, "001")
    not_nominated = _car(db_session, show, "002")
    _judge_and_maybe_nominate(db_session, show, hh1, nominated, 25, award=award)
    _judge_and_maybe_nominate(db_session, show, hh2, not_nominated, 25, award=None, closed_at_uptime_ms=1)

    ranking = award_rankings(db_session, show, award)

    assert [e.car.id for e in ranking] == [nominated.id]


def test_suggest_winner_unambiguous(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    winner = _car(db_session, show, "001")
    runner_up = _car(db_session, show, "002")
    _judge_and_maybe_nominate(db_session, show, hh1, winner, 25, award=award)
    _judge_and_maybe_nominate(db_session, show, hh2, runner_up, 20, award=award, closed_at_uptime_ms=1)

    suggestion = suggest_award_winner(db_session, show, award)

    assert suggestion.ambiguous is False
    assert suggestion.entry.car.id == winner.id


def test_suggest_winner_ambiguous_when_nominees_tie(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car1 = _car(db_session, show, "001")
    car2 = _car(db_session, show, "002")
    _judge_and_maybe_nominate(db_session, show, hh1, car1, 25, award=award)
    _judge_and_maybe_nominate(db_session, show, hh2, car2, 25, award=award, closed_at_uptime_ms=1)

    suggestion = suggest_award_winner(db_session, show, award)

    assert suggestion.entry is None
    assert suggestion.ambiguous is True


def test_suggest_winner_none_when_no_nominees(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    suggestion = suggest_award_winner(db_session, show, award)
    assert suggestion.entry is None
    assert suggestion.ambiguous is False


def test_high_scorers_not_nominated_flags_cars_at_or_above_the_winners_score(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    winner = _car(db_session, show, "001")
    also_25_not_nominated = _car(db_session, show, "002")
    lower_not_nominated = _car(db_session, show, "003")
    _judge_and_maybe_nominate(db_session, show, hh1, winner, 25, award=award)
    _judge_and_maybe_nominate(db_session, show, hh2, also_25_not_nominated, 25, award=None, closed_at_uptime_ms=1)
    _judge_and_maybe_nominate(db_session, show, hh1, lower_not_nominated, 18, award=None, closed_at_uptime_ms=2)

    flagged = high_scorers_not_nominated(db_session, show, award)

    flagged_ids = {car.id for car, score in flagged}
    assert also_25_not_nominated.id in flagged_ids
    assert lower_not_nominated.id not in flagged_ids
    assert winner.id not in flagged_ids  # nominated cars are never flagged, they're already in the running


def test_high_scorers_not_nominated_empty_when_nobody_scored(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    assert high_scorers_not_nominated(db_session, show, award) == []


def test_nominations_per_judge_report_computes_rate(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car1 = _car(db_session, show, "001")
    car2 = _car(db_session, show, "002")
    car3 = _car(db_session, show, "003")
    _judge_and_maybe_nominate(db_session, show, hh1, car1, 25, award=award)
    _judge_and_maybe_nominate(db_session, show, hh1, car2, 10, award=None, closed_at_uptime_ms=1)
    _judge_and_maybe_nominate(db_session, show, hh2, car3, 25, award=award, closed_at_uptime_ms=2)

    report = {r.handheld_label: r for r in nominations_per_judge_report(db_session, show.id)}

    assert report["hh-1"].cars_judged == 2
    assert report["hh-1"].nominations_made == 1
    assert report["hh-1"].rate == 0.5
    assert report["hh-2"].cars_judged == 1
    assert report["hh-2"].nominations_made == 1
    assert report["hh-2"].rate == 1.0


def test_choose_winner_and_clear(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car = _car(db_session, show, "001")
    revision_before = show.configuration_revision

    choose_winner(db_session, show, award, car.id)
    db_session.refresh(award)
    assert award.winner_car_id == car.id
    assert show.configuration_revision == revision_before + 1

    choose_winner(db_session, show, award, None)
    db_session.refresh(award)
    assert award.winner_car_id is None


def test_use_suggested_winner_confirms_the_suggestion(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    winner = _car(db_session, show, "001")
    _judge_and_maybe_nominate(db_session, show, hh1, winner, 25, award=award)

    error = use_suggested_winner(db_session, show, award)

    assert error is None
    db_session.refresh(award)
    assert award.winner_car_id == winner.id


def test_use_suggested_winner_errors_when_ambiguous(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car1 = _car(db_session, show, "001")
    car2 = _car(db_session, show, "002")
    _judge_and_maybe_nominate(db_session, show, hh1, car1, 25, award=award)
    _judge_and_maybe_nominate(db_session, show, hh2, car2, 25, award=award, closed_at_uptime_ms=1)

    error = use_suggested_winner(db_session, show, award)

    assert error is not None
    db_session.refresh(award)
    assert award.winner_car_id is None


def test_mark_not_presented_clears_any_winner(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car = _car(db_session, show, "001")
    choose_winner(db_session, show, award, car.id)

    mark_not_presented(db_session, show, award, True)

    db_session.refresh(award)
    assert award.marked_not_presented is True
    assert award.winner_car_id is None


def test_choosing_a_winner_un_marks_not_presented(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car = _car(db_session, show, "001")
    mark_not_presented(db_session, show, award, True)

    choose_winner(db_session, show, award, car.id)

    db_session.refresh(award)
    assert award.marked_not_presented is False


def test_set_announcer_name_bumps_show_data_revision(db_session, paint_award_setup):
    show, paint, hh1, hh2, award = paint_award_setup
    car = _car(db_session, show, "001")
    revision_before = show.show_data_revision

    set_announcer_name(db_session, show, car, "  Jamie Diaz  ")

    db_session.refresh(car)
    assert car.announcer_name == "Jamie Diaz"
    assert show.show_data_revision == revision_before + 1
