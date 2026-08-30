"""
Automatic score-range escalation — see CONTEXT.md's "Judging categories &
scoring range" section. The range is never chosen by the organiser and it
only ever moves up: once a show crosses a car-count threshold, the range
holds at that level for the rest of the show even if cars are later
removed. Removing a car must never recalculate other cars' scores
downward — check_and_escalate() enforces this by construction (it only
ever raises show.score_range_max, never lowers it, and no other code
path writes to that field).

Escalating means every already-recorded JudgingScore (and, when the show
has Overall Impression enabled, every submission's Overall Impression
score) gets its adjusted_points recomputed — always from original_points/
original_range_max via services/score_conversion.py, never from a
previously adjusted value, so repeated escalations across a show never
compound rounding error.
"""
from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.models import Car, JudgingScore, JudgingSubmission, Show
from app.services.revisions import bump_configuration_revision
from app.services.score_conversion import convert_score

# CONTEXT.md: 1-150 -> 1-5, 151-300 -> 1-10, 301+ -> 1-25 (open-ended).
_TIER_THRESHOLDS = [(150, 5), (300, 10)]
_TOP_TIER_MAX = 25


def range_max_for_car_count(car_count: int) -> int:
    for threshold, range_max in _TIER_THRESHOLDS:
        if car_count <= threshold:
            return range_max
    return _TOP_TIER_MAX


def check_and_escalate(db: Session, show: Show) -> bool:
    """Recomputes the target range from the show's current car count and
    escalates if that target is higher than the show's current range.
    Returns True if an escalation happened, False otherwise (including
    when the target is lower or equal — the range never lowers, and a
    call that changes nothing is a safe, cheap no-op, i.e. idempotent)."""
    car_count = db.scalar(select(func.count(Car.id)).where(Car.show_id == show.id)) or 0
    target = range_max_for_car_count(car_count)

    if target <= show.score_range_max:
        return False

    show.score_range_max = target

    scores = db.scalars(
        select(JudgingScore).join(JudgingSubmission).where(JudgingSubmission.show_id == show.id)
    )
    for score in scores:
        score.adjusted_points = convert_score(score.original_points, score.original_range_max, target)

    submissions_with_overall_impression = db.scalars(
        select(JudgingSubmission).where(
            JudgingSubmission.show_id == show.id,
            JudgingSubmission.overall_impression_original_points.is_not(None),
        )
    )
    for submission in submissions_with_overall_impression:
        submission.overall_impression_adjusted_points = convert_score(
            submission.overall_impression_original_points,
            submission.overall_impression_original_range_max,
            target,
        )

    bump_configuration_revision(db, show)
    db.commit()
    return True
