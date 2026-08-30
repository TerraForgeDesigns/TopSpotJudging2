"""
Sync protocol business logic — see /PROTOCOL.md. The API layer
(app/api/sync.py) is a thin wire-format wrapper around this module.
"""
from dataclasses import dataclass
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.api.schemas import ScoreIn, SubmissionIn
from app.models import (
    Car,
    CarClass,
    CarStatus,
    Handheld,
    JudgingCriteria,
    JudgingScore,
    JudgingSubmission,
    Show,
    SubmissionStatus,
)
from app.services.dashboard import get_summary

# Wire protocol only knows three statuses (PROTOCOL.md POST /sync/submissions
# response). SubmissionStatus.UNMATCHED and .PENDING are home-base-internal —
# a handheld doesn't need to know reconciliation is pending, only that its
# data was received and won't be lost. See DECISIONS.md.
_WIRE_STATUS = {
    SubmissionStatus.ACCEPTED: "accepted",
    SubmissionStatus.UNMATCHED: "accepted",
    SubmissionStatus.FLAGGED_DUPLICATE: "flagged_duplicate",
    SubmissionStatus.REJECTED: "error",
    SubmissionStatus.PENDING: "error",
}


def list_unmatched_submissions(db: Session, show_id: int) -> list[JudgingSubmission]:
    """For the host reconciliation view — see DECISIONS.md."""
    return list(
        db.scalars(
            select(JudgingSubmission)
            .where(JudgingSubmission.show_id == show_id, JudgingSubmission.status == SubmissionStatus.UNMATCHED)
            .options(selectinload(JudgingSubmission.scores).selectinload(JudgingScore.criteria))
            .order_by(JudgingSubmission.submitted_at.desc())
        )
    )


def reconcile_unmatched_for_car(db: Session, car: Car) -> None:
    """Called right after a car is created (single add or CSV import) —
    promotes any UNMATCHED submissions sitting under its registration_number
    into normal accepted/flagged_duplicate submissions now that the car
    exists. Same first-one-wins rule as process_submission: the earliest
    received becomes the accepted one, any others flag a conflict."""
    pending = list(
        db.scalars(
            select(JudgingSubmission)
            .where(
                JudgingSubmission.show_id == car.show_id,
                JudgingSubmission.registration_number == car.registration_number,
                JudgingSubmission.status == SubmissionStatus.UNMATCHED,
            )
            .order_by(JudgingSubmission.submitted_at)
        )
    )
    if not pending:
        return

    first, *rest = pending
    first.car_id = car.id
    first.status = SubmissionStatus.ACCEPTED
    car.status = CarStatus.JUDGED

    for extra in rest:
        extra.car_id = car.id
        extra.status = SubmissionStatus.FLAGGED_DUPLICATE
        car.status = CarStatus.FLAGGED_CONFLICT

    db.commit()


def get_or_create_handheld(db: Session, handheld_id: str) -> Handheld:
    """The wire protocol's handheld_id is a string the firmware assigns
    itself (see PROTOCOL.md's `"handheld_id": "hh-2"` example) — there's no
    provisioning UI yet, so the first sync from an unseen id auto-creates its
    Handheld row, keyed on label."""
    handheld = db.scalars(select(Handheld).where(Handheld.label == handheld_id)).first()
    if handheld is None:
        handheld = Handheld(label=handheld_id)
        db.add(handheld)
        db.commit()
        db.refresh(handheld)
    return handheld


def touch_handheld_sync(db: Session, handheld: Handheld, client_ip: str | None) -> None:
    handheld.last_sync_at = datetime.now(timezone.utc)
    if client_ip:
        handheld.last_ip = client_ip
    db.commit()


def _serialize_car(car: Car) -> dict:
    return {
        "id": car.id,
        "registration_number": car.registration_number,
        "display_car_number": car.display_car_number,
        "make": car.make,
        "model": car.model,
        "year": car.year,
        "class_name": car.car_class.name if car.car_class else None,
        "status": car.status.value,
        "updated_at": car.updated_at,
    }


def _serialize_class(car_class: CarClass) -> dict:
    return {"id": car_class.id, "name": car_class.name}


def _serialize_criteria(criteria: JudgingCriteria) -> dict:
    return {"id": criteria.id, "name": criteria.name, "max_points": criteria.max_points}


def get_roster_snapshot(db: Session, show: Show, since: datetime | None) -> dict:
    """Shape matches PROTOCOL.md's roster payload minus server_time (the
    caller adds that — GET /sync/roster and POST /sync/submissions both
    need this same shape, the latter nested as `roster_delta`)."""
    car_query = select(Car).where(Car.show_id == show.id).options(selectinload(Car.car_class))
    if since is not None:
        car_query = car_query.where(Car.updated_at > since)
    cars = list(db.scalars(car_query.order_by(Car.registration_number)))

    criteria_query = select(JudgingCriteria).where(
        JudgingCriteria.show_id == show.id, JudgingCriteria.active.is_(True)
    )
    if since is not None:
        criteria_query = criteria_query.where(JudgingCriteria.updated_at > since)
    criteria = list(db.scalars(criteria_query.order_by(JudgingCriteria.sort_order)))

    # CarClass has no updated_at column (see models/car_class.py) so it can't
    # be delta-filtered — always sent in full. Classes are few and rarely
    # change, so this is cheap; see DECISIONS.md.
    classes = list(db.scalars(select(CarClass).where(CarClass.show_id == show.id).order_by(CarClass.sort_order)))

    summary = get_summary(db, show.id)

    return {
        "cars": [_serialize_car(c) for c in cars],
        "classes": [_serialize_class(c) for c in classes],
        "criteria": [_serialize_criteria(c) for c in criteria],
        "summary": {
            "total_cars": summary.total_cars,
            "judged": summary.judged,
            "unjudged": summary.unjudged,
            "flagged_conflict": summary.flagged_conflict,
        },
    }


@dataclass
class SubmissionResult:
    registration_number: str
    status: str
    message: str = ""


def _resolve_scores(
    show: Show, scores_in: list[ScoreIn]
) -> tuple[list[tuple[JudgingCriteria, int]], list[str]]:
    """Matches by name, case-insensitive, against every criterion the show
    has ever had (not just active ones) — deactivating a criterion stops it
    appearing in future roster pulls, but a score a judge already recorded
    against it before deactivation must still resolve. Returns
    (resolved (criteria, points) pairs, unknown names)."""
    by_name = {c.name.strip().lower(): c for c in show.criteria}
    resolved: list[tuple[JudgingCriteria, int]] = []
    unknown: list[str] = []
    for score_in in scores_in:
        criteria = by_name.get(score_in.criteria_name.strip().lower())
        if criteria is None:
            unknown.append(score_in.criteria_name)
        else:
            resolved.append((criteria, score_in.points))
    return resolved, unknown


def _find_repeat(db: Session, registration_number: str, handheld_id: int, closed_at: datetime) -> JudgingSubmission | None:
    """Idempotency: the exact same (registration_number, handheld, closed_at)
    tuple showing up again is a retried ack, not a new submission — a judge
    closes a car once; a handheld resending after a lost ack must never
    manufacture a false conflict. See PROTOCOL.md duplicate handling and
    tests/test_sync.py::test_same_submission_retry_is_not_flagged."""
    return db.scalars(
        select(JudgingSubmission).where(
            JudgingSubmission.registration_number == registration_number,
            JudgingSubmission.handheld_id == handheld_id,
            JudgingSubmission.closed_at == closed_at,
        )
    ).first()


def process_submission(db: Session, show: Show, handheld: Handheld, item: SubmissionIn) -> SubmissionResult:
    registration_number = item.registration_number.strip()

    resolved_scores, unknown_names = _resolve_scores(show, item.scores)
    if unknown_names:
        # Reject the whole item rather than importing a partial score set —
        # a car's total only means something if every criterion was
        # recorded; silently dropping one would be worse than rejecting all
        # of it (see CONTEXT.md: failures must be visible, never silent).
        return SubmissionResult(
            registration_number=registration_number,
            status="error",
            message=f"Unknown judging criteria: {', '.join(unknown_names)}",
        )

    repeat = _find_repeat(db, registration_number, handheld.id, item.closed_at)
    if repeat is not None:
        return SubmissionResult(
            registration_number=registration_number,
            status=_WIRE_STATUS[repeat.status],
            message="Already recorded — repeat of a submission already received.",
        )

    car = db.scalars(
        select(Car).where(Car.show_id == show.id, Car.registration_number == registration_number)
    ).first()

    if car is None:
        # Late-registered car judged before the handheld's roster pull
        # caught up. Held for host reconciliation rather than rejected
        # outright — see DECISIONS.md.
        submission = JudgingSubmission(
            show_id=show.id,
            car_id=None,
            registration_number=registration_number,
            handheld_id=handheld.id,
            judge_name=item.judge_name,
            closed_at=item.closed_at,
            status=SubmissionStatus.UNMATCHED,
        )
        db.add(submission)
        db.flush()
        for criteria, points in resolved_scores:
            db.add(JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=points))
        db.commit()
        return SubmissionResult(
            registration_number=registration_number,
            status="accepted",
            message="Registration number not found in roster yet — held for host reconciliation.",
        )

    existing_accepted = db.scalars(
        select(JudgingSubmission).where(
            JudgingSubmission.car_id == car.id, JudgingSubmission.status == SubmissionStatus.ACCEPTED
        )
    ).first()

    if existing_accepted is not None:
        # A genuine conflict (different handheld, or same handheld scoring
        # this car a second time with a different closed_at) — the repeat
        # check above already ruled out an exact retry. Original accepted
        # submission is never touched.
        submission = JudgingSubmission(
            show_id=show.id,
            car_id=car.id,
            registration_number=registration_number,
            handheld_id=handheld.id,
            judge_name=item.judge_name,
            closed_at=item.closed_at,
            status=SubmissionStatus.FLAGGED_DUPLICATE,
        )
        db.add(submission)
        db.flush()
        for criteria, points in resolved_scores:
            db.add(JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=points))
        car.status = CarStatus.FLAGGED_CONFLICT
        db.commit()
        return SubmissionResult(
            registration_number=registration_number,
            status="flagged_duplicate",
            message="Car already has an accepted submission — host must resolve.",
        )

    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=registration_number,
        handheld_id=handheld.id,
        judge_name=item.judge_name,
        closed_at=item.closed_at,
        status=SubmissionStatus.ACCEPTED,
    )
    db.add(submission)
    db.flush()
    for criteria, points in resolved_scores:
        db.add(JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=points))
    car.status = CarStatus.JUDGED
    db.commit()
    return SubmissionResult(registration_number=registration_number, status="accepted", message="")
