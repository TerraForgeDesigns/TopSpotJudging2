"""
Sync business logic for POST /api/v1/sync — see PROTOCOL.md. The API
layer (app/api/sync.py) is a thin wire-format wrapper; this module owns
idempotency, score conversion on receipt, revision bookkeeping, and
applying a submission's details onto its Car.

Entries are pre-created 001..N at show creation, so there is no more
"unmatched, held" submission state — an entry_number either resolves to
an existing Car or the whole item is rejected as an error. Idempotency
key is (entry_number, handheld_id, closed_at_uptime_ms), not a timestamp.

process_submission()'s order, matching PROTOCOL.md exactly:
  1. Idempotency check — a retry of an already-recorded submission must
     be recognized before anything else, or a lost-ack retry from a
     roaming handheld manufactures a false conflict every time.
  2. Validate the item as a whole (unknown category, a missing score for
     an active category, an out-of-range score) — reject the WHOLE item
     before writing anything, never a partial score set.
  3. Duplicate-conflict check against any existing accepted result.
  4. Accept: convert scores, create the result/scores/nominations, mark
     the car judged.
  5. Apply entry details (fill blanks always; overwrite a differing
     non-empty value and log the correction).
  6. Record a New Vehicle Names candidate sighting if either manually-
     entered flag is set.
"""
import logging
from dataclasses import dataclass
from datetime import datetime, timezone

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.api.schemas import CarOut, CategoryOut, ConfigurationOut, NominationOptionOut, ProtocolSummary, SubmissionIn
from app.models import (
    Award,
    AwardNomination,
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
from app.services.score_conversion import convert_score
from app.services.vehicle_candidates import record_sighting as record_vehicle_candidate_sighting

logger = logging.getLogger(__name__)


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


def touch_handheld_sync(
    db: Session,
    handheld: Handheld,
    client_ip: str | None,
    battery_pct: int | None,
    config_revision: int,
    data_revision: int,
) -> None:
    handheld.last_sync_at = datetime.now(timezone.utc)
    if client_ip:
        handheld.last_ip = client_ip
    if battery_pct is not None:
        handheld.battery_pct = battery_pct
    handheld.last_config_revision = config_revision
    handheld.last_data_revision = data_revision
    db.commit()


def get_protocol_summary(db: Session, show_id: int) -> ProtocolSummary:
    """The four fields PROTOCOL.md's `summary` object specifies, always
    computed from the FULL current roster, never scoped to a delta — see
    PROTOCOL.md. Deliberately its own query, not shared with
    services/dashboard.py's DashboardSummary — see DECISIONS.md and
    api/schemas.py's ProtocolSummary docstring."""
    counts = dict(
        db.execute(select(Car.status, func.count(Car.id)).where(Car.show_id == show_id).group_by(Car.status)).all()
    )
    return ProtocolSummary(
        total_cars=sum(counts.values()),
        judged=counts.get(CarStatus.JUDGED, 0),
        unjudged=counts.get(CarStatus.UNJUDGED, 0),
        flagged_conflict=counts.get(CarStatus.FLAGGED_CONFLICT, 0),
    )


def get_configuration(db: Session, show: Show) -> ConfigurationOut:
    categories = list(
        db.scalars(
            select(JudgingCategory)
            .where(JudgingCategory.show_id == show.id, JudgingCategory.active.is_(True))
            .order_by(JudgingCategory.sort_order)
        )
    )
    judge_chosen_awards = list(
        db.scalars(select(Award).where(Award.show_id == show.id, Award.judge_chosen.is_(True)))
    )
    return ConfigurationOut(
        show_name=show.name,
        score_range_max=show.score_range_max,
        max_score=len(categories) * show.score_range_max,
        overall_impression_enabled=show.overall_impression_enabled,
        categories=[CategoryOut(id=c.id, name=c.name, sort_order=c.sort_order) for c in categories],
        judge_chosen_awards=[NominationOptionOut(id=a.id, name=a.name) for a in judge_chosen_awards],
    )


def get_cars_delta(db: Session, show: Show, since_data_revision: int) -> list[CarOut]:
    cars = list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show.id, Car.data_revision_at_change > since_data_revision)
            .order_by(Car.entry_number)
        )
    )
    return [
        CarOut(
            id=c.id,
            entry_number=c.entry_number,
            participant=c.participant,
            year=c.year,
            make=c.make,
            model=c.model,
            vehicle_type=c.vehicle_type,
            status=c.status.value,
        )
        for c in cars
    ]


@dataclass
class SubmissionResult:
    entry_number: str
    status: str
    message: str = ""


def _find_repeat(db: Session, entry_number: str, handheld_id: int, closed_at_uptime_ms: int) -> JudgingSubmission | None:
    """Idempotency: the exact same (entry_number, handheld, closed_at_uptime_ms)
    triple showing up again is a retried ack, not a new submission — a judge
    closes a car once; a handheld resending after a lost ack must never
    manufacture a false conflict. See PROTOCOL.md."""
    return db.scalars(
        select(JudgingSubmission).where(
            JudgingSubmission.entry_number == entry_number,
            JudgingSubmission.handheld_id == handheld_id,
            JudgingSubmission.closed_at_uptime_ms == closed_at_uptime_ms,
        )
    ).first()


def _validate_scores(
    show: Show, item: SubmissionIn
) -> tuple[list[tuple[JudgingCategory, int]], str | None]:
    """Returns (resolved (category, points) pairs, error message). A
    non-None error means reject the WHOLE item — a car's total only
    means something if every active category was recorded, and an
    out-of-range point value is a data problem, not something to clamp
    silently. See CONTEXT.md: no zero, no "not applicable," minimum 1."""
    categories_by_id = {c.id: c for c in show.categories}
    active_categories = {c.id: c for c in show.categories if c.active}

    resolved: list[tuple[JudgingCategory, int]] = []
    problems: list[str] = []
    seen_category_ids: set[int] = set()

    for score_in in item.scores:
        category = categories_by_id.get(score_in.category_id)
        if category is None:
            problems.append(f"Unknown judging category id {score_in.category_id}.")
            continue
        seen_category_ids.add(category.id)
        if not (1 <= score_in.points <= item.score_range_max):
            problems.append(
                f"{category.name} score of {score_in.points} is outside the allowed range "
                f"(1-{item.score_range_max})."
            )
            continue
        resolved.append((category, score_in.points))

    missing = [c for cid, c in active_categories.items() if cid not in seen_category_ids]
    for category in missing:
        problems.append(f"{category.name} has not been scored. Choose a {category.name} score before continuing.")

    if problems:
        return [], " ".join(problems)
    return resolved, None


def _apply_details(db: Session, show: Show, car: Car, item: SubmissionIn) -> None:
    """Whoever reaches an entry first fills it in (CONTEXT.md). A blank
    field is always filled from the submission. A field that already has
    a value is only overwritten when the handheld's value is non-empty
    AND different — that's a judge correcting a wrong pre-fill, not a
    conflict, so it's applied directly and logged (diagnostic log, not a
    user-facing message — see LANGUAGE.md: technical detail belongs in
    logs, not the interface)."""
    for field in ("participant", "year", "make", "model", "vehicle_type"):
        incoming = (getattr(item, field) or "").strip()
        if not incoming:
            continue
        current = getattr(car, field)
        if current is None:
            setattr(car, field, incoming)
        elif incoming != current:
            logger.info(
                "Entry %s: %s corrected from %r to %r by handheld submission.",
                car.entry_number, field, current, incoming,
            )
            setattr(car, field, incoming)

    if item.make_manually_entered:
        car.make_manually_entered = True
    if item.model_manually_entered:
        car.model_manually_entered = True

    if item.make_manually_entered or item.model_manually_entered:
        record_vehicle_candidate_sighting(db, show, car, item.make, item.model)


def process_submission(db: Session, show: Show, handheld: Handheld, item: SubmissionIn) -> SubmissionResult:
    entry_number = item.entry_number.strip()

    # 1. Idempotency — checked before anything else, including whether the
    # entry number even exists, so a retry is always recognized as such.
    repeat = _find_repeat(db, entry_number, handheld.id, item.closed_at_uptime_ms)
    if repeat is not None:
        return SubmissionResult(
            entry_number=entry_number,
            status="already_recorded",
            message="Already recorded — repeat of a submission already received.",
        )

    car = db.scalars(select(Car).where(Car.show_id == show.id, Car.entry_number == entry_number)).first()
    if car is None:
        return SubmissionResult(
            entry_number=entry_number,
            status="error",
            message=f"Entry number '{entry_number}' not found.",
        )

    # 2. Validate the whole item before writing anything.
    resolved_scores, error = _validate_scores(show, item)
    if error:
        return SubmissionResult(entry_number=entry_number, status="error", message=error)

    # 3. Duplicate-conflict check — a second accepted result never
    # overwrites the first, regardless of which handheld sent it.
    existing_accepted = db.scalars(
        select(JudgingSubmission).where(
            JudgingSubmission.car_id == car.id, JudgingSubmission.status == SubmissionStatus.ACCEPTED
        )
    ).first()
    status = SubmissionStatus.FLAGGED_DUPLICATE if existing_accepted is not None else SubmissionStatus.ACCEPTED

    # 4. Accept: convert and store.
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        entry_number=entry_number,
        handheld_id=handheld.id,
        judge_name=item.judge_name,
        closed_at=datetime.now(timezone.utc),
        closed_at_uptime_ms=item.closed_at_uptime_ms,
        status=status,
    )
    if item.overall_impression is not None and show.overall_impression_enabled:
        submission.overall_impression_original_points = item.overall_impression
        submission.overall_impression_original_range_max = item.score_range_max
        submission.overall_impression_adjusted_points = convert_score(
            item.overall_impression, item.score_range_max, show.score_range_max
        )
    db.add(submission)
    db.flush()

    for category, points in resolved_scores:
        db.add(
            JudgingScore(
                submission_id=submission.id,
                category_id=category.id,
                original_points=points,
                original_range_max=item.score_range_max,
                adjusted_points=convert_score(points, item.score_range_max, show.score_range_max),
            )
        )

    for award_id in item.nominations:
        award = db.get(Award, award_id)
        if award is not None and award.show_id == show.id and award.judge_chosen:
            db.add(AwardNomination(submission_id=submission.id, award_id=award_id))

    if status == SubmissionStatus.ACCEPTED:
        # 5 + 6. Apply details (fill/correct) and record a vehicle
        # candidate sighting — only for the accepted result; a
        # flagged_duplicate never touches the car's own details.
        _apply_details(db, show, car, item)
        car.status = CarStatus.JUDGED
    else:
        car.status = CarStatus.FLAGGED_CONFLICT

    bump_show_data_revision(db, show)
    car.data_revision_at_change = show.show_data_revision
    db.commit()

    if status == SubmissionStatus.ACCEPTED:
        return SubmissionResult(entry_number=entry_number, status="accepted", message="")
    return SubmissionResult(
        entry_number=entry_number,
        status="flagged_duplicate",
        message="Car already has an accepted submission — host must resolve.",
    )
