"""
Post-creation Show Award management — see CONTEXT.md's Awards section
and Edit Show. CONTEXT.md is explicit that none of this has protection
rules: add/rename/reorder/toggle/change-winner-choice are all always
allowed, including while the show is running — this file confirms none
of them are silently gated, and that every mutation bumps
configuration_revision.
"""
from datetime import date, datetime, timezone

import pytest

from app.models import (
    Award,
    AwardNomination,
    Car,
    CarStatus,
    Handheld,
    JudgingSubmission,
    Show,
    SubmissionStatus,
)
from app.models.enums import AwardRankingBasis
from app.services.awards_setup import (
    add_award,
    reorder_awards,
    rename_award,
    set_winner_choice,
    toggle_award,
)


@pytest.fixture()
def show_with_award(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field")
    db_session.add(show)
    db_session.commit()
    award = Award(show_id=show.id, name="Best Paint", judge_chosen=True, ranking_basis=AwardRankingBasis.PAINT, sort_order=0)
    db_session.add(award)
    db_session.commit()
    return show, award


def _judge_a_car(db, show):
    handheld = Handheld(label="hh-1")
    car = Car(show_id=show.id, entry_number="001", status=CarStatus.JUDGED, data_revision_at_change=1)
    db.add_all([handheld, car])
    db.flush()
    submission = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number="001", handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=0, status=SubmissionStatus.ACCEPTED,
    )
    db.add(submission)
    db.commit()
    return submission


def test_add_award_allowed_even_after_judging(db_session, show_with_award):
    show, existing = show_with_award
    _judge_a_car(db_session, show)
    revision_before = show.configuration_revision

    result = add_award(db_session, show, "Sponsor's Choice", judge_chosen=False, ranking_basis=None)

    assert result.errors == {}
    assert result.award is not None
    assert result.award.name == "Sponsor's Choice"
    assert result.award.judge_chosen is False
    assert show.configuration_revision == revision_before + 1


def test_add_award_validates_name_and_ranking_basis(db_session, show_with_award):
    show, existing = show_with_award

    missing_name = add_award(db_session, show, "", judge_chosen=True, ranking_basis=AwardRankingBasis.TOTAL)
    assert "name" in missing_name.errors

    missing_basis = add_award(db_session, show, "Best Interior", judge_chosen=True, ranking_basis=None)
    assert "ranking_basis" in missing_basis.errors

    organiser_choice_needs_no_basis = add_award(db_session, show, "Mayor's Choice", judge_chosen=False, ranking_basis=None)
    assert organiser_choice_needs_no_basis.errors == {}


def test_rename_allowed_even_after_judging(db_session, show_with_award):
    show, award = show_with_award
    _judge_a_car(db_session, show)
    revision_before = show.configuration_revision

    rename_award(db_session, show, award, "Best Custom Paint")

    assert award.name == "Best Custom Paint"
    assert show.configuration_revision == revision_before + 1


def test_toggle_allowed_even_after_judging(db_session, show_with_award):
    show, award = show_with_award
    _judge_a_car(db_session, show)
    revision_before = show.configuration_revision

    toggle_award(db_session, show, award)

    assert award.active is False
    assert show.configuration_revision == revision_before + 1


def test_reorder_allowed_even_after_judging(db_session, show_with_award):
    show, award = show_with_award
    second = Award(show_id=show.id, name="Best Engine", judge_chosen=True, ranking_basis=AwardRankingBasis.ENGINE, sort_order=1)
    db_session.add(second)
    db_session.commit()
    _judge_a_car(db_session, show)
    revision_before = show.configuration_revision

    reorder_awards(db_session, show, [second.id, award.id])

    assert second.sort_order == 0
    assert award.sort_order == 1
    assert show.configuration_revision == revision_before + 1


def test_set_winner_choice_to_organiser_chosen_keeps_nominations(db_session, show_with_award):
    show, award = show_with_award
    submission = _judge_a_car(db_session, show)
    nomination = AwardNomination(submission_id=submission.id, award_id=award.id)
    db_session.add(nomination)
    db_session.commit()
    nomination_id = nomination.id
    revision_before = show.configuration_revision

    set_winner_choice(db_session, show, award, judge_chosen=False, ranking_basis=None)

    assert award.judge_chosen is False
    assert award.ranking_basis is None
    assert show.configuration_revision == revision_before + 1
    # CONTEXT.md: existing nominations stay in the database, just stop
    # being used to determine the winner — never deleted here.
    assert db_session.get(AwardNomination, nomination_id) is not None


def test_set_winner_choice_to_judge_chosen_sets_ranking_basis(db_session, show_with_award):
    show, award = show_with_award
    set_winner_choice(db_session, show, award, judge_chosen=False, ranking_basis=None)

    set_winner_choice(db_session, show, award, judge_chosen=True, ranking_basis=AwardRankingBasis.TOTAL)

    assert award.judge_chosen is True
    assert award.ranking_basis == AwardRankingBasis.TOTAL
