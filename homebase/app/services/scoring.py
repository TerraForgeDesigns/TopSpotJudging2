"""
Total scoring and rankings. A car's total is the sum of points across its
ACCEPTED submission's scores — see CONTEXT.md's judging workflow. Cars
with no accepted submission are unscored, not zero, and are excluded from
every ranking entirely (never a phantom last-place 0-point entry).

There is no automatic tiebreak rule yet (see DECISIONS.md) — rank_by_score
flags ties instead of silently ordering by insertion/id, and is the single
place every ranking (overall, per-class, per-criterion) computes rank from.
A future tiebreak rule plugs in as a secondary sort key inside this one
function; no caller or template needs to change.
"""
from collections import Counter
from dataclasses import dataclass

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import Car, CarClass, JudgingCriteria, JudgingSubmission, SubmissionStatus


def get_accepted_submission(car: Car) -> JudgingSubmission | None:
    for submission in car.submissions:
        if submission.status == SubmissionStatus.ACCEPTED:
            return submission
    return None


def submission_total(submission: JudgingSubmission) -> int:
    return sum(score.points for score in submission.scores)


def list_scored_cars(db: Session, show_id: int) -> list[tuple[Car, JudgingSubmission]]:
    """Cars that actually have an accepted submission — unscored cars are
    excluded entirely, never included as a 0-point entry."""
    cars = list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show_id)
            .options(
                selectinload(Car.submissions).selectinload(JudgingSubmission.scores),
                selectinload(Car.car_class),
            )
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
    score: int
    tied: bool
    car: Car


def rank_by_score(entries: list[tuple[Car, int]]) -> list[RankedEntry]:
    """Standard competition ("1224") ranking: equal scores share a rank,
    and the next distinct score jumps to (position + 1), not (rank + 1).
    Every tied rank is flagged — see module docstring."""
    ordered = sorted(entries, key=lambda e: e[1], reverse=True)
    ranked: list[RankedEntry] = []
    for index, (car, score) in enumerate(ordered):
        rank = index + 1 if index == 0 or score != ordered[index - 1][1] else ranked[-1].rank
        ranked.append(RankedEntry(rank=rank, score=score, tied=False, car=car))
    counts = Counter(entry.rank for entry in ranked)
    for entry in ranked:
        entry.tied = counts[entry.rank] > 1
    return ranked


def overall_rankings(db: Session, show_id: int) -> list[RankedEntry]:
    scored = list_scored_cars(db, show_id)
    return rank_by_score([(car, submission_total(submission)) for car, submission in scored])


def class_rankings(db: Session, show_id: int) -> list[tuple[CarClass | None, list[RankedEntry]]]:
    """One ranking per class that actually has a scored car, in class
    sort_order, followed by Unclassified last if it has any scored cars."""
    scored = list_scored_cars(db, show_id)
    by_class: dict[int | None, list[tuple[Car, int]]] = {}
    for car, submission in scored:
        by_class.setdefault(car.class_id, []).append((car, submission_total(submission)))

    classes = list(db.scalars(select(CarClass).where(CarClass.show_id == show_id).order_by(CarClass.sort_order)))
    groups: list[tuple[CarClass | None, list[RankedEntry]]] = [
        (car_class, rank_by_score(by_class[car_class.id])) for car_class in classes if car_class.id in by_class
    ]
    if None in by_class:
        groups.append((None, rank_by_score(by_class[None])))
    return groups


def criteria_leaderboards(db: Session, show_id: int) -> list[tuple[JudgingCriteria, list[RankedEntry]]]:
    """One leaderboard per criterion the show has ever had (including
    deactivated ones — deactivating only stops future judging, historical
    scores stay visible, see DECISIONS.md), skipping any with no scores."""
    scored = list_scored_cars(db, show_id)
    criteria_list = list(
        db.scalars(select(JudgingCriteria).where(JudgingCriteria.show_id == show_id).order_by(JudgingCriteria.sort_order))
    )
    boards = []
    for criteria in criteria_list:
        entries = []
        for car, submission in scored:
            points = next((s.points for s in submission.scores if s.criteria_id == criteria.id), None)
            if points is not None:
                entries.append((car, points))
        if entries:
            boards.append((criteria, rank_by_score(entries)))
    return boards
