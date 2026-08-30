"""
Results — see CONTEXT.md's Awards section (Top Awards) and this task's
Results requirements. Built on services/scoring.py's ranking kernel;
this module owns the one piece of Top Awards that isn't purely computed
from rankings — resolving a tie that straddles the Nth place boundary —
plus assembling data for the Results page, CSV export, and print view.
"""
from dataclasses import dataclass, field

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import JudgingCategory, Show
from app.services.revisions import bump_configuration_revision
from app.services.scoring import RankedEntry, entries_at_rank, get_accepted_submission, overall_rankings


@dataclass
class TopAwardsResult:
    placed: list[RankedEntry]  # definitively placed, len <= top_n
    tied_group: list[RankedEntry]  # cars tied at the boundary needing a decision (empty if none)
    slots_remaining: int  # how many of tied_group still need to be chosen
    resolved: bool  # True if there's no ambiguity, or the host has resolved it


def compute_top_awards(
    ranked: list[RankedEntry], top_n: int, resolved_car_ids: set[int] | None = None
) -> TopAwardsResult:
    """The top_n cars by `ranked` (already in rank order — see
    scoring.py::overall_rankings). If the Nth place falls in the middle
    of a tied group (more cars share that rank than there are slots
    left), that group can't be resolved by ranking alone — CONTEXT.md:
    "4 cars are tied for the last 2 places in the Top 50. Choose which
    cars place." `resolved_car_ids` is the host's prior choice (see
    Show.top_awards_resolved_car_ids) — cars from the tied group they've
    explicitly picked to fill the remaining slots.
    """
    resolved_car_ids = resolved_car_ids or set()
    if not ranked or top_n <= 0:
        return TopAwardsResult(placed=[], tied_group=[], slots_remaining=0, resolved=True)
    if top_n >= len(ranked):
        return TopAwardsResult(placed=list(ranked), tied_group=[], slots_remaining=0, resolved=True)

    boundary_rank = ranked[top_n - 1].rank
    within = [e for e in ranked if e.rank < boundary_rank]
    at_boundary = entries_at_rank(ranked, boundary_rank)
    slots_remaining = top_n - len(within)

    if len(at_boundary) <= slots_remaining:
        # Every tied car fits — no real choice to make, they're equally
        # deserving and there's room for all of them.
        return TopAwardsResult(placed=within + at_boundary, tied_group=[], slots_remaining=0, resolved=True)

    chosen = [e for e in at_boundary if e.car.id in resolved_car_ids]
    if resolved_car_ids and len(chosen) == slots_remaining:
        return TopAwardsResult(
            placed=within + chosen, tied_group=at_boundary, slots_remaining=slots_remaining, resolved=True
        )
    return TopAwardsResult(placed=within, tied_group=at_boundary, slots_remaining=slots_remaining, resolved=False)


def get_top_awards(db: Session, show: Show) -> TopAwardsResult | None:
    """None means Top Awards hasn't been configured for this show yet
    (Show.top_awards_count is None) — distinct from "configured but the
    show has no cars/scores yet," which is a normal empty TopAwardsResult."""
    if show.top_awards_count is None:
        return None
    ranked = overall_rankings(db, show.id)
    resolved_ids = set(show.top_awards_resolved_car_ids or [])
    return compute_top_awards(ranked, show.top_awards_count, resolved_ids)


def resolve_top_awards(db: Session, show: Show, car_ids: set[int]) -> None:
    """The host's explicit choice of which tied cars fill the remaining
    Top Awards slots — see compute_top_awards()'s docstring. Stores only
    the chosen ids, not a recomputed placement list, since the ranking
    itself is always derived fresh. Same revision counter as an award
    winner decision (choose_winner in award_results.py): this is a
    results/awards-presentation choice, not a car-data edit."""
    show.top_awards_resolved_car_ids = sorted(car_ids)
    bump_configuration_revision(db, show)
    db.commit()


@dataclass
class ResultsBoardExport:
    """Flat rows for CSV export and the print view — see web/results.py."""

    entry_number: str
    participant: str
    year: str
    make: str
    model: str
    rank: int
    tied: bool
    total: int
    category_scores: dict = field(default_factory=dict)  # category name -> adjusted points


def build_results_export(db: Session, show: Show) -> list[ResultsBoardExport]:
    """Flattens overall_rankings() into rows for CSV export and the print
    view — one row per scored car, in rank order, with every active
    category's adjusted score alongside the total. Unscored cars are
    excluded here too, same as the ranking itself."""
    categories = list(
        db.scalars(
            select(JudgingCategory)
            .where(JudgingCategory.show_id == show.id, JudgingCategory.active.is_(True))
            .order_by(JudgingCategory.sort_order)
        )
    )
    rows = []
    for entry in overall_rankings(db, show.id):
        submission = get_accepted_submission(entry.car)
        scores_by_category_id = {score.category_id: score.adjusted_points for score in submission.scores}
        rows.append(
            ResultsBoardExport(
                entry_number=entry.car.entry_number,
                participant=entry.car.participant or "",
                year=entry.car.year or "",
                make=entry.car.make or "",
                model=entry.car.model or "",
                rank=entry.rank,
                tied=entry.tied,
                total=entry.key[0],
                category_scores={c.name: scores_by_category_id.get(c.id) for c in categories},
            )
        )
    return rows
