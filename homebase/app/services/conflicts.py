"""
Conflict resolution — see CONTEXT.md: a car is judged exactly once; when
two handhelds both produce an accepted-looking submission for the same
entry (services/sync.py never overwrites the first), the host resolves
it here by hand. Nothing is ever silently dropped — every competing
submission stays queryable for audit even after resolution, only one is
ever ACCEPTED at a time.
"""
from dataclasses import dataclass, field
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import (
    Car,
    CarStatus,
    Handheld,
    JudgingCategory,
    JudgingScore,
    JudgingSubmission,
    Show,
    SubmissionStatus,
)
from app.services.revisions import bump_show_data_revision
from app.services.sync import get_or_create_handheld

# Submissions the host enters by hand (a corrected score set) are
# attributed to this synthetic handheld, not a physical device — reuses
# the same get-or-create-by-label mechanism the wire protocol uses for
# real handhelds.
HOST_HANDHELD_LABEL = "Home Base (host correction)"


def list_conflicted_cars(db: Session, show_id: int) -> list[Car]:
    return list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show_id, Car.status == CarStatus.FLAGGED_CONFLICT)
            .options(
                selectinload(Car.submissions).selectinload(JudgingSubmission.scores).selectinload(JudgingScore.category),
                selectinload(Car.submissions).selectinload(JudgingSubmission.handheld),
            )
            .order_by(Car.entry_number)
        )
    )


@dataclass
class ConflictRow:
    submission: JudgingSubmission
    scores_by_category_id: dict = field(default_factory=dict)
    total: int = 0


@dataclass
class ConflictView:
    car: Car
    categories: list[JudgingCategory]
    rows: list[ConflictRow]


def build_conflict_view(car: Car) -> ConflictView:
    """Side-by-side comparison of every competing submission for `car` —
    categories as rows, submissions (judge, handheld, time, per-category
    score, total) as columns, matching how a host naturally compares
    judge sheets. "Car 148 was judged twice. Choose which scores to
    keep." is built from this in the template."""
    categories_seen: dict[int, JudgingCategory] = {}
    for submission in car.submissions:
        for score in submission.scores:
            categories_seen[score.category.id] = score.category
    categories = sorted(categories_seen.values(), key=lambda c: c.sort_order)

    # Non-rejected first (what still needs a decision), then by submit time.
    ordered_submissions = sorted(
        car.submissions, key=lambda s: (s.status == SubmissionStatus.REJECTED, s.submitted_at)
    )
    rows = [
        ConflictRow(
            submission=submission,
            scores_by_category_id={score.category_id: score.adjusted_points for score in submission.scores},
            total=sum(score.adjusted_points for score in submission.scores),
        )
        for submission in ordered_submissions
    ]
    return ConflictView(car=car, categories=categories, rows=rows)


def accept_submission(db: Session, show: Show, car: Car, submission_id: int) -> None:
    """Host picks one competing submission as the real one — every other
    submission on this car becomes REJECTED (kept for audit, never
    deleted), and the car is judged."""
    chosen = db.get(JudgingSubmission, submission_id)
    if chosen is None or chosen.car_id != car.id:
        raise ValueError("That submission does not belong to this car.")

    for submission in car.submissions:
        submission.status = SubmissionStatus.ACCEPTED if submission.id == chosen.id else SubmissionStatus.REJECTED
    car.status = CarStatus.JUDGED
    bump_show_data_revision(db, show)
    car.data_revision_at_change = show.show_data_revision
    db.commit()


def create_corrected_submission(
    db: Session, show: Show, car: Car, scores: dict[int, int], note: str
) -> JudgingSubmission:
    """Host enters a corrected score set instead of picking an existing
    submission — e.g. neither judge sheet was transcribed correctly.
    Every existing submission on this car is rejected in favor of this
    new one. Points are stored as both original and adjusted at the
    show's CURRENT range — a host typing scores in directly is typing
    them at today's range, there is no separate "original" to preserve."""
    for submission in car.submissions:
        if submission.status in (SubmissionStatus.ACCEPTED, SubmissionStatus.FLAGGED_DUPLICATE):
            submission.status = SubmissionStatus.REJECTED

    handheld = get_or_create_handheld(db, HOST_HANDHELD_LABEL)
    new_submission = JudgingSubmission(
        show_id=car.show_id,
        car_id=car.id,
        entry_number=car.entry_number,
        handheld_id=handheld.id,
        judge_name=None,
        note=note.strip() or None,
        closed_at=datetime.now(timezone.utc),
        closed_at_uptime_ms=0,
        status=SubmissionStatus.ACCEPTED,
    )
    db.add(new_submission)
    db.flush()
    for category_id, points in scores.items():
        db.add(
            JudgingScore(
                submission_id=new_submission.id,
                category_id=category_id,
                original_points=points,
                original_range_max=show.score_range_max,
                adjusted_points=points,
            )
        )
    car.status = CarStatus.JUDGED
    bump_show_data_revision(db, show)
    car.data_revision_at_change = show.show_data_revision
    db.commit()
    db.refresh(new_submission)
    return new_submission
