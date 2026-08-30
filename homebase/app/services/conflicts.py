"""
Conflict resolution (flagged_conflict cars with competing submissions) and
manual reconciliation of unmatched submissions. See CONTEXT.md — a car is
judged exactly once; when two accepted-looking submissions collide, the
host resolves it by hand, and nothing is ever silently dropped.
"""
from dataclasses import dataclass, field
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import Car, CarStatus, JudgingCriteria, JudgingScore, JudgingSubmission, SubmissionStatus
from app.services.sync import get_or_create_handheld

# Submissions the host enters by hand (corrected score, resolving a
# conflict) are attributed to this synthetic handheld rather than a
# physical device — reuses the same get-or-create-by-label mechanism the
# wire protocol uses for real handhelds (see services/sync.py).
HOST_HANDHELD_LABEL = "Home Base (host correction)"


def list_conflicted_cars(db: Session, show_id: int) -> list[Car]:
    return list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show_id, Car.status == CarStatus.FLAGGED_CONFLICT)
            .options(
                selectinload(Car.submissions).selectinload(JudgingSubmission.scores).selectinload(JudgingScore.criteria),
                selectinload(Car.submissions).selectinload(JudgingSubmission.handheld),
            )
            .order_by(Car.registration_number)
        )
    )


def submission_total(submission: JudgingSubmission) -> int:
    return sum(score.points for score in submission.scores)


@dataclass
class ConflictRow:
    submission: JudgingSubmission
    scores_by_criteria_id: dict = field(default_factory=dict)
    total: int = 0


@dataclass
class ConflictView:
    car: Car
    criteria: list[JudgingCriteria]
    rows: list[ConflictRow]


def build_conflict_view(car: Car) -> ConflictView:
    """Side-by-side comparison of every competing submission for `car` —
    criteria as rows, submissions as columns, matching how a host would
    naturally compare judge sheets."""
    criteria_seen: dict[int, JudgingCriteria] = {}
    for submission in car.submissions:
        for score in submission.scores:
            criteria_seen[score.criteria.id] = score.criteria
    criteria_list = sorted(criteria_seen.values(), key=lambda c: c.sort_order)

    # Non-rejected first (what still needs a decision), then by submit time.
    ordered_submissions = sorted(
        car.submissions, key=lambda s: (s.status == SubmissionStatus.REJECTED, s.submitted_at)
    )
    rows = [
        ConflictRow(
            submission=submission,
            scores_by_criteria_id={score.criteria_id: score.points for score in submission.scores},
            total=submission_total(submission),
        )
        for submission in ordered_submissions
    ]
    return ConflictView(car=car, criteria=criteria_list, rows=rows)


def accept_submission(db: Session, car_id: int, submission_id: int) -> None:
    """Host picks one competing submission as the real one — every other
    submission on this car becomes REJECTED (kept for audit, never
    deleted), and the car is judged."""
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")
    chosen = db.get(JudgingSubmission, submission_id)
    if chosen is None or chosen.car_id != car_id:
        raise ValueError("That submission does not belong to this car.")

    for submission in car.submissions:
        submission.status = SubmissionStatus.ACCEPTED if submission.id == chosen.id else SubmissionStatus.REJECTED
    car.status = CarStatus.JUDGED
    db.commit()


def create_corrected_submission(db: Session, car_id: int, scores: dict[int, int], note: str) -> JudgingSubmission:
    """Host enters a corrected score set instead of picking an existing
    submission — e.g. neither judge sheet was transcribed correctly. Every
    existing submission on this car is rejected in favor of this new one."""
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")

    for submission in car.submissions:
        if submission.status in (SubmissionStatus.ACCEPTED, SubmissionStatus.FLAGGED_DUPLICATE, SubmissionStatus.PENDING):
            submission.status = SubmissionStatus.REJECTED

    handheld = get_or_create_handheld(db, HOST_HANDHELD_LABEL)
    new_submission = JudgingSubmission(
        show_id=car.show_id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        judge_name=None,
        note=note.strip() or None,
        closed_at=datetime.now(timezone.utc),
        status=SubmissionStatus.ACCEPTED,
    )
    db.add(new_submission)
    db.flush()
    for criteria_id, points in scores.items():
        db.add(JudgingScore(submission_id=new_submission.id, criteria_id=criteria_id, points=points))
    car.status = CarStatus.JUDGED
    db.commit()
    db.refresh(new_submission)
    return new_submission


def assign_unmatched_submission(db: Session, submission_id: int, car_id: int) -> None:
    """Host manually assigns a held (UNMATCHED) submission to a car —
    reuses the same accepted-vs-conflict logic a normal sync would."""
    submission = db.get(JudgingSubmission, submission_id)
    if submission is None:
        raise ValueError(f"No submission with id {submission_id}")
    if submission.status != SubmissionStatus.UNMATCHED:
        return  # already resolved — a double-submitted form is a no-op
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")

    existing_accepted = db.scalars(
        select(JudgingSubmission).where(
            JudgingSubmission.car_id == car.id, JudgingSubmission.status == SubmissionStatus.ACCEPTED
        )
    ).first()

    submission.car_id = car.id
    submission.registration_number = car.registration_number
    if existing_accepted is not None:
        submission.status = SubmissionStatus.FLAGGED_DUPLICATE
        car.status = CarStatus.FLAGGED_CONFLICT
    else:
        submission.status = SubmissionStatus.ACCEPTED
        car.status = CarStatus.JUDGED
    db.commit()


def discard_unmatched_submission(db: Session, submission_id: int) -> None:
    submission = db.get(JudgingSubmission, submission_id)
    if submission is None:
        raise ValueError(f"No submission with id {submission_id}")
    if submission.status != SubmissionStatus.UNMATCHED:
        return
    submission.status = SubmissionStatus.REJECTED
    db.commit()
