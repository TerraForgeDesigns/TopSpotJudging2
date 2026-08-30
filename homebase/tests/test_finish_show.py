"""
services/finish_show.py — the finish-show gate. See CONTEXT.md's
"Protecting completed work" and this task's Finishing the Show
requirement: every award needs a winner or an explicit not-presented
mark, including a judge-chosen award with zero nominations.
"""
from datetime import date

import pytest

from app.models import Award, Car, Show
from app.models.enums import AwardRankingBasis, ShowStatus
from app.services.award_results import choose_winner, mark_not_presented
from app.services.finish_show import can_finish_show, finish_show, outstanding_awards


@pytest.fixture()
def show(db_session):
    show = Show(name="Show", event_date=date(2026, 9, 1))
    db_session.add(show)
    db_session.commit()
    return show


def test_no_awards_means_nothing_outstanding(db_session, show):
    assert outstanding_awards(db_session, show) == []
    assert can_finish_show(db_session, show) is True


def test_organiser_chosen_award_with_no_winner_is_outstanding(db_session, show):
    award = Award(show_id=show.id, name="Sponsor's Choice", judge_chosen=False)
    db_session.add(award)
    db_session.commit()

    outstanding = outstanding_awards(db_session, show)

    assert len(outstanding) == 1
    assert "Sponsor's Choice" in outstanding[0].reason
    assert can_finish_show(db_session, show) is False


def test_judge_chosen_award_with_zero_nominations_is_outstanding_by_name(db_session, show):
    award = Award(show_id=show.id, name="Best Rat Rod", judge_chosen=True, ranking_basis=AwardRankingBasis.TOTAL)
    db_session.add(award)
    db_session.commit()

    outstanding = outstanding_awards(db_session, show)

    assert len(outstanding) == 1
    assert "Best Rat Rod" in outstanding[0].reason
    assert "no cars were nominated" in outstanding[0].reason.lower()


def test_award_with_winner_is_not_outstanding(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()
    award = Award(show_id=show.id, name="Best Paint", judge_chosen=False)
    db_session.add(award)
    db_session.commit()

    choose_winner(db_session, show, award, car.id)

    assert outstanding_awards(db_session, show) == []
    assert can_finish_show(db_session, show) is True


def test_award_marked_not_presented_is_not_outstanding(db_session, show):
    award = Award(show_id=show.id, name="Memorial Award", judge_chosen=False)
    db_session.add(award)
    db_session.commit()

    mark_not_presented(db_session, show, award, True)

    assert outstanding_awards(db_session, show) == []


def test_inactive_award_is_never_outstanding(db_session, show):
    award = Award(show_id=show.id, name="Retired Award", judge_chosen=False, active=False)
    db_session.add(award)
    db_session.commit()

    assert outstanding_awards(db_session, show) == []


def test_finish_show_fails_and_changes_nothing_while_outstanding(db_session, show):
    award = Award(show_id=show.id, name="Sponsor's Choice", judge_chosen=False)
    db_session.add(award)
    db_session.commit()
    show.status = ShowStatus.JUDGING
    db_session.commit()

    result = finish_show(db_session, show)

    assert result is False
    assert show.status == ShowStatus.JUDGING


def test_finish_show_succeeds_once_everything_is_resolved(db_session, show):
    award = Award(show_id=show.id, name="Sponsor's Choice", judge_chosen=False)
    db_session.add(award)
    db_session.commit()
    mark_not_presented(db_session, show, award, True)
    show.status = ShowStatus.JUDGING
    db_session.commit()

    result = finish_show(db_session, show)

    assert result is True
    assert show.status == ShowStatus.FINISHED
