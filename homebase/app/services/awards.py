"""
Award slots. A host defines what's being awarded (services here handle
CRUD); the winner is auto-suggested from the relevant ranking in
services/scoring.py and stored in Award.winner_car_id only once the host
confirms or overrides it — that column is the single source of truth for
what actually gets announced.
"""
from dataclasses import dataclass

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import Award, AwardCategory
from app.services.scoring import RankedEntry, class_rankings, criteria_leaderboards, overall_rankings


def list_awards(db: Session, show_id: int) -> list[Award]:
    return list(
        db.scalars(
            select(Award)
            .where(Award.show_id == show_id)
            .options(
                selectinload(Award.criteria),
                selectinload(Award.car_class),
                selectinload(Award.winner_car),
            )
            .order_by(Award.sort_order)
        )
    )


def get_award(db: Session, award_id: int) -> Award | None:
    return db.get(Award, award_id)


def create_award(
    db: Session,
    show_id: int,
    name: str,
    category: AwardCategory,
    criteria_id: int | None = None,
    class_id: int | None = None,
) -> Award:
    max_order = db.scalar(select(Award.sort_order).where(Award.show_id == show_id).order_by(Award.sort_order.desc()))
    award = Award(
        show_id=show_id,
        name=name.strip(),
        category=category,
        criteria_id=criteria_id if category == AwardCategory.CRITERIA else None,
        class_id=class_id if category == AwardCategory.CLASS else None,
        sort_order=(max_order or 0) + 1,
    )
    db.add(award)
    db.commit()
    db.refresh(award)
    return award


def delete_award(db: Session, award_id: int) -> None:
    award = db.get(Award, award_id)
    if award is None:
        raise ValueError(f"No award with id {award_id}")
    db.delete(award)
    db.commit()


def set_winner(db: Session, award_id: int, car_id: int | None) -> Award:
    """car_id=None clears the assignment (host wants to go back to the
    auto-suggestion, or hasn't decided yet)."""
    award = db.get(Award, award_id)
    if award is None:
        raise ValueError(f"No award with id {award_id}")
    award.winner_car_id = car_id
    db.commit()
    db.refresh(award)
    return award


@dataclass
class AwardSuggestion:
    entry: RankedEntry | None  # None if there's nothing scored yet to suggest from
    ambiguous: bool  # True if rank 1 is a tie — no unambiguous suggestion


def suggest_winner(db: Session, award: Award) -> AwardSuggestion:
    """Rank-1 car from the relevant leaderboard, or an "ambiguous" flag if
    rank 1 is itself a tie — ties never get silently auto-picked."""
    if award.category == AwardCategory.OVERALL:
        ranking = overall_rankings(db, award.show_id)
    elif award.category == AwardCategory.CRITERIA:
        boards = criteria_leaderboards(db, award.show_id)
        ranking = next((entries for criteria, entries in boards if criteria.id == award.criteria_id), [])
    elif award.category == AwardCategory.CLASS:
        groups = class_rankings(db, award.show_id)
        ranking = next((entries for car_class, entries in groups if car_class and car_class.id == award.class_id), [])
    else:
        ranking = []

    leaders = [entry for entry in ranking if entry.rank == 1]
    if not leaders:
        return AwardSuggestion(entry=None, ambiguous=False)
    if len(leaders) > 1:
        return AwardSuggestion(entry=None, ambiguous=True)
    return AwardSuggestion(entry=leaders[0], ambiguous=False)
