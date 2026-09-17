from dataclasses import dataclass
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.api.schemas import ScoreIn, SubmissionIn
from app.models import (
    Award,
    AwardNomination,
    Car,
    CarStatus,
    JudgingCategory,
    JudgingScore,
    JudgingSubmission,
    Show,
    SubmissionSource,
    SubmissionStatus,
)
from app.services.revisions import bump_show_data_revision
from app.services.score_conversion import convert_score
from app.services.scoring import get_accepted_submission
from app.services.sync import _apply_details, _canonical_entry_number, _validate_scores


@dataclass
class ManualJudgeContext:
    car: Car
    existing_submission: JudgingSubmission | None
    categories: list[JudgingCategory]
    awards: list[Award]
    score_range_max: int
    max_score: int


@dataclass
class ManualJudgeResult:
    ok: bool
    message: str
    car: Car | None = None
    submission: JudgingSubmission | None = None


def get_manual_judge_context(db: Session, show: Show, car_id: int) -> ManualJudgeContext | None:
    car = db.scalars(
        select(Car)
        .where(Car.id == car_id, Car.show_id == show.id)
        .options(selectinload(Car.submissions).selectinload(JudgingSubmission.scores))
    ).first()
    if car is None:
        return None
    categories = list(
        db.scalars(
            select(JudgingCategory)
            .where(JudgingCategory.show_id == show.id, JudgingCategory.active.is_(True))
            .order_by(JudgingCategory.sort_order)
        )
    )
    awards = list(
        db.scalars(
            select(Award)
            .where(Award.show_id == show.id, Award.active.is_(True), Award.judge_chosen.is_(True))
            .order_by(Award.sort_order)
        )
    )
    return ManualJudgeContext(
        car=car,
        existing_submission=get_accepted_submission(car),
        categories=categories,
        awards=awards,
        score_range_max=show.score_range_max,
        max_score=len(categories) * show.score_range_max,
    )


def manual_judge_car(
    db: Session,
    show: Show,
    car_id: int,
    scores_by_category_id: dict[int, int],
    nomination_ids: list[int],
    participant: str = "",
    year: str = "",
    make: str = "",
    model: str = "",
    operator_label: str = "",
    replace_existing: bool = False,
) -> ManualJudgeResult:
    ctx = get_manual_judge_context(db, show, car_id)
    if ctx is None:
        return ManualJudgeResult(False, "Entry not found.")
    if ctx.existing_submission is not None and not replace_existing:
        return ManualJudgeResult(False, "Already judged.", car=ctx.car, submission=ctx.existing_submission)

    item = SubmissionIn(
        local_record_id=None,
        entry_number=ctx.car.entry_number,
        closed_at_uptime_ms=0,
        participant=participant,
        year=year,
        make=make,
        model=model,
        vehicle_type=ctx.car.vehicle_type,
        score_range_max=show.score_range_max,
        scores=[
            ScoreIn(category_id=category.id, points=scores_by_category_id.get(category.id, 0))
            for category in ctx.categories
        ],
        nominations=nomination_ids,
        vehicle_photo_path=None,
        judge_sheet_photo_path=None,
    )
    entry_number, car = _canonical_entry_number(db, show, ctx.car.entry_number)
    if car is None:
        return ManualJudgeResult(False, f"Entry number '{ctx.car.entry_number}' not found.")
    resolved_scores, error = _validate_scores(show, item)
    if error:
        return ManualJudgeResult(False, error, car=car)

    if ctx.existing_submission is not None and replace_existing:
        ctx.existing_submission.status = SubmissionStatus.REJECTED
        ctx.existing_submission.note = "Replaced by Home Base manual correction."

    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        entry_number=entry_number,
        handheld_id=None,
        judge_name=None,
        source=SubmissionSource.HOMEBASE_MANUAL,
        operator_label=operator_label.strip() or None,
        vehicle_photo_required=False,
        judge_sheet_photo_required=False,
        vehicle_photo_path=None,
        judge_sheet_photo_path=None,
        closed_at=datetime.now(timezone.utc),
        closed_at_uptime_ms=0,
        status=SubmissionStatus.ACCEPTED,
    )
    db.add(submission)
    db.flush()

    for category, points in resolved_scores:
        db.add(
            JudgingScore(
                submission_id=submission.id,
                category_id=category.id,
                original_points=points,
                original_range_max=show.score_range_max,
                adjusted_points=convert_score(points, show.score_range_max, show.score_range_max),
            )
        )

    active_award_ids = {award.id for award in ctx.awards}
    for award_id in nomination_ids:
        if award_id in active_award_ids:
            db.add(AwardNomination(submission_id=submission.id, award_id=award_id))

    _apply_details(db, show, car, item)
    car.status = CarStatus.JUDGED
    bump_show_data_revision(db, show)
    car.data_revision_at_change = show.show_data_revision
    db.commit()
    db.refresh(submission)
    db.refresh(car)
    return ManualJudgeResult(True, "Manual judging saved.", car=car, submission=submission)
