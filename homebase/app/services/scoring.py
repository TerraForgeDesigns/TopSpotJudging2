"""
Total scoring and rankings. A car's total is the sum of adjusted_points
across its ACCEPTED submission's scores — see CONTEXT.md's judging
workflow. Cars with no accepted submission are unscored, not zero, and
are excluded from every ranking entirely (never a phantom last-place
0-point entry).

THE RANKING KERNEL — rank_by_tuple() — implements CONTEXT.md's tie-break
cascade: total adjusted score, then Overall Impression (if the show has
it enabled), then each active Judging Category in the organiser's
priority order, compared one at a time. That's not "a secondary sort
key" bolted onto a simple score — it's a single ORDERED TUPLE per car,
built fresh per ranking call by build_ranking_key() so a category
priority reorder takes effect immediately with no stored data to touch.
Every element of a "higher is better" tuple sorts correctly via Python's
native tuple comparison (lexicographic, descending), so the kernel itself
stays a plain sort — the cascade's complexity lives entirely in how the
tuple gets built, not in the comparison.

entries_at_rank() exposes tie membership BY POSITION, not just "a tie
exists" — e.g. HB7 needs to say "4 cars are tied for the last 2 places in
the Top 50," which means knowing exactly which RankedEntry rows share a
boundary rank, not just a boolean.
"""
from collections import Counter
from dataclasses import dataclass

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import Car, JudgingCategory, JudgingSubmission, Show, SubmissionStatus


def get_accepted_submission(car: Car) -> JudgingSubmission | None:
    for submission in car.submissions:
        if submission.status == SubmissionStatus.ACCEPTED:
            return submission
    return None


def submission_total(submission: JudgingSubmission) -> int:
    return sum(score.adjusted_points for score in submission.scores)


def list_scored_cars(db: Session, show_id: int) -> list[tuple[Car, JudgingSubmission]]:
    """Cars that actually have an accepted submission — unscored cars are
    excluded entirely, never included as a 0-point entry."""
    cars = list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show_id)
            .options(selectinload(Car.submissions).selectinload(JudgingSubmission.scores))
        )
    )
    result = []
    for car in cars:
        submission = get_accepted_submission(car)
        if submission is not None:
            result.append((car, submission))
    return result


@dataclass
class RankedEntry:
    rank: int
    car: Car
    key: tuple
    tied: bool = False


def rank_by_tuple(entries: list[tuple[Car, tuple]]) -> list[RankedEntry]:
    """Standard competition ("1224") ranking over an ORDERED TUPLE of
    comparison values per car, most significant element first. Equal keys
    share a rank and the next distinct key jumps to (position + 1), not
    (rank + 1) — see module docstring. Two cars are only truly tied if
    every element of their tuples matches."""
    ordered = sorted(entries, key=lambda e: e[1], reverse=True)
    ranked: list[RankedEntry] = []
    for index, (car, key) in enumerate(ordered):
        rank = index + 1 if index == 0 or key != ordered[index - 1][1] else ranked[-1].rank
        ranked.append(RankedEntry(rank=rank, car=car, key=key))
    counts = Counter(entry.rank for entry in ranked)
    for entry in ranked:
        entry.tied = counts[entry.rank] > 1
    return ranked


def entries_at_rank(ranked: list[RankedEntry], rank: int) -> list[RankedEntry]:
    """Every entry sharing `rank` — the exact tie membership a Top-N
    boundary (or any other rank) needs surfaced, not just a bool."""
    return [entry for entry in ranked if entry.rank == rank]


def build_ranking_key(
    show: Show, submission: JudgingSubmission, categories_in_priority_order: list[JudgingCategory]
) -> tuple:
    """The ordered tuple CONTEXT.md's tie-break cascade compares: total
    adjusted score, then Overall Impression (only when the show has it
    enabled), then each active category's adjusted score in the
    organiser's priority order (categories_in_priority_order — pass
    JudgingCategory.sort_order-ordered active categories; that ordering
    IS the priority order, see models/category.py)."""
    total = submission_total(submission)
    key: list[int] = [total]
    if show.overall_impression_enabled:
        key.append(submission.overall_impression_adjusted_points or 0)
    scores_by_category_id = {score.category_id: score.adjusted_points for score in submission.scores}
    for category in categories_in_priority_order:
        key.append(scores_by_category_id.get(category.id, 0))
    return tuple(key)


def _active_categories_in_priority_order(db: Session, show_id: int) -> list[JudgingCategory]:
    return list(
        db.scalars(
            select(JudgingCategory)
            .where(JudgingCategory.show_id == show_id, JudgingCategory.active.is_(True))
            .order_by(JudgingCategory.sort_order)
        )
    )


def overall_rankings(db: Session, show_id: int) -> list[RankedEntry]:
    show = db.get(Show, show_id)
    scored = list_scored_cars(db, show_id)
    categories = _active_categories_in_priority_order(db, show_id)
    entries = [(car, build_ranking_key(show, submission, categories)) for car, submission in scored]
    return rank_by_tuple(entries)


def category_leaderboards(db: Session, show_id: int) -> list[tuple[JudgingCategory, list[RankedEntry]]]:
    """One leaderboard per category the show has ever had (including
    inactive ones — deactivating only stops future judging; historical
    scores stay visible), skipping any with no scores. Ranked by that
    category's adjusted score alone (a single-element tuple) — the fuller
    award-specific tie cascade (basis -> Overall Impression -> Total) is
    HB7's concern, not this kernel's."""
    scored = list_scored_cars(db, show_id)
    categories = list(
        db.scalars(select(JudgingCategory).where(JudgingCategory.show_id == show_id).order_by(JudgingCategory.sort_order))
    )
    boards = []
    for category in categories:
        entries = []
        for car, submission in scored:
            points = next((s.adjusted_points for s in submission.scores if s.category_id == category.id), None)
            if points is not None:
                entries.append((car, (points,)))
        if entries:
            boards.append((category, rank_by_tuple(entries)))
    return boards
