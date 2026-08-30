"""
Post-creation Show Award management — see CONTEXT.md's Awards section
and Edit Show. Named awards_setup.py, not awards.py, to leave that name
free for HB7's winner-resolution logic (nomination ranking, Top Awards,
Choose Winner) — this module only manages the award SLOTS, never
computes a winner.

Unlike Judging Categories, awards have NO protection rules: CONTEXT.md
is explicit that adding, renaming, reordering, turning on/off, and
changing who chooses the winner are "always allowed, including while
the show is running." Every mutation bumps configuration_revision via
services/revisions.py — never a direct column write.
"""
from dataclasses import dataclass, field

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.models import Award, Show
from app.models.enums import AwardRankingBasis
from app.services.revisions import bump_configuration_revision

# What "change who chooses the winner" offers when switching an award TO
# judge-chosen — see CONTEXT.md's "Which score should decide the winner?"
RANKING_BASIS_OPTIONS = [
    (AwardRankingBasis.TOTAL, "Total Score"),
    (AwardRankingBasis.ENGINE, "Engine"),
    (AwardRankingBasis.EXTERIOR, "Exterior"),
    (AwardRankingBasis.INTERIOR, "Interior"),
    (AwardRankingBasis.PAINT, "Paint"),
    (AwardRankingBasis.WHEELS_TIRES, "Wheels / Tires"),
]
RANKING_BASIS_LABELS = {basis: label for basis, label in RANKING_BASIS_OPTIONS}


def list_awards(db: Session, show_id: int) -> list[Award]:
    return list(db.scalars(select(Award).where(Award.show_id == show_id).order_by(Award.sort_order)))


def get_award(db: Session, award_id: int) -> Award | None:
    return db.get(Award, award_id)


@dataclass
class AddAwardResult:
    errors: dict[str, str] = field(default_factory=dict)
    award: Award | None = None


def add_award(
    db: Session, show: Show, name: str, judge_chosen: bool, ranking_basis: AwardRankingBasis | None
) -> AddAwardResult:
    errors: dict[str, str] = {}
    name = (name or "").strip()
    if not name:
        errors["name"] = "Award Name is required. Enter a name before adding the award."
    if judge_chosen and ranking_basis is None:
        errors["ranking_basis"] = "Choose which score should decide the winner before adding the award."
    if errors:
        return AddAwardResult(errors=errors)

    max_order = db.scalar(select(func.max(Award.sort_order)).where(Award.show_id == show.id))
    award = Award(
        show_id=show.id,
        name=name,
        judge_chosen=judge_chosen,
        ranking_basis=ranking_basis if judge_chosen else None,
        sort_order=(max_order or 0) + 1,
    )
    db.add(award)
    bump_configuration_revision(db, show)
    db.commit()
    db.refresh(award)
    return AddAwardResult(award=award)


def rename_award(db: Session, show: Show, award: Award, name: str) -> None:
    name = (name or "").strip()
    if not name:
        return
    award.name = name
    bump_configuration_revision(db, show)
    db.commit()


def toggle_award(db: Session, show: Show, award: Award) -> None:
    award.active = not award.active
    bump_configuration_revision(db, show)
    db.commit()


def reorder_awards(db: Session, show: Show, ordered_ids: list[int]) -> None:
    order_index = {award_id: i for i, award_id in enumerate(ordered_ids)}
    awards = list(db.scalars(select(Award).where(Award.show_id == show.id)))
    for award in awards:
        if award.id in order_index:
            award.sort_order = order_index[award.id]
    bump_configuration_revision(db, show)
    db.commit()


def set_winner_choice(
    db: Session, show: Show, award: Award, judge_chosen: bool, ranking_basis: AwardRankingBasis | None
) -> None:
    """CONTEXT.md: switching an award from judge-chosen to
    organiser-chosen mid-show keeps existing AwardNomination rows in the
    database but stops using them for the winner — this function never
    deletes a nomination, it only flips judge_chosen/ranking_basis on the
    Award itself. Surfacing "existing nominations are being kept but
    ignored now" to the host is the web layer's job (a plain-language
    notice), not this function's."""
    award.judge_chosen = judge_chosen
    award.ranking_basis = ranking_basis if judge_chosen else None
    bump_configuration_revision(db, show)
    db.commit()
