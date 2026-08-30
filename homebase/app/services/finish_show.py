"""
The finish-show gate — see CONTEXT.md's "Protecting completed work" and
this task's Finishing the Show requirement. Every active Show Award must
have a winner or be explicitly marked not-presented before a show can
finish — including a judge-chosen award that received zero nominations,
which CONTEXT.md calls out by name as needing the same resolution, not a
silent skip.
"""
from dataclasses import dataclass

from sqlalchemy.orm import Session

from app.models import Award, Show
from app.models.enums import ShowStatus
from app.services.award_results import award_rankings
from app.services.awards_setup import list_awards


@dataclass
class OutstandingAward:
    award: Award
    reason: str


def outstanding_awards(db: Session, show: Show) -> list[OutstandingAward]:
    """Exactly what's blocking finish, by award name, and what to do —
    never a generic "some awards aren't ready." An inactive award was
    never going to be presented, so it's not outstanding."""
    outstanding: list[OutstandingAward] = []
    for award in list_awards(db, show.id):
        if not award.active or award.marked_not_presented or award.winner_car_id is not None:
            continue
        if award.judge_chosen:
            has_nominees = len(award_rankings(db, show, award)) > 0
            if has_nominees:
                reason = (
                    f'"{award.name}" has nominees but no confirmed winner yet. '
                    "Choose a winner or mark it as not presented."
                )
            else:
                reason = f'"{award.name}" — no cars were nominated. Choose a winner or mark it as not presented.'
        else:
            reason = f'"{award.name}" has no winner chosen. Choose a winner or mark it as not presented.'
        outstanding.append(OutstandingAward(award=award, reason=reason))
    return outstanding


def can_finish_show(db: Session, show: Show) -> bool:
    return len(outstanding_awards(db, show)) == 0


def finish_show(db: Session, show: Show) -> bool:
    """Returns False (and changes nothing) if any award is still
    outstanding — the caller must never call this and assume success
    without checking the return value. No revision bump: a show finishing
    doesn't change anything a handheld's sync response needs to reflect,
    unlike every other Show Setup or entry change."""
    if not can_finish_show(db, show):
        return False
    show.status = ShowStatus.FINISHED
    db.commit()
    return True
