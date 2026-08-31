"""
The guard rules behind CONTEXT.md's "Protecting completed work" section,
for Judging Categories specifically, in both directions:

  - Turning a category OFF once it has scores against it would strand
    those scores out of every future total/ranking computed from active
    categories only.
  - Turning a category ON after cars have already been judged (without
    it) leaves those cars permanently missing a score for it — their
    totals are short a whole category, and nothing will ever go back
    and score them for it.

Called from services/judging_categories.py, which is the post-creation
management layer (Edit Show's Judging Setup section) — kept as a
separate module deliberately, so the rule itself stays a checkable,
independently testable function rather than a conditional buried in a
web route. Pure query + message functions: no mutation, no commit — the
caller checks `can_deactivate_category`/`can_activate_category` before
flipping `JudgingCategory.active` itself.
"""
from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.models import JudgingCategory, JudgingScore, JudgingSubmission, SubmissionStatus


def judged_car_count_for_category(db: Session, category: JudgingCategory) -> int:
    """Distinct cars with an ACCEPTED submission carrying a score for this
    category — "how many cars were judged with it," not a raw score-row
    count (which could include rejected/duplicate submissions)."""
    return (
        db.scalar(
            select(func.count(func.distinct(JudgingSubmission.car_id)))
            .select_from(JudgingScore)
            .join(JudgingSubmission, JudgingScore.submission_id == JudgingSubmission.id)
            .where(JudgingScore.category_id == category.id, JudgingSubmission.status == SubmissionStatus.ACCEPTED)
        )
        or 0
    )


def can_deactivate_category(db: Session, category: JudgingCategory) -> str | None:
    """Returns a plain-language reason it can't be turned off, or None if
    it's safe to. Blocks only when the category already has scores —
    matches CONTEXT.md exactly ("Turning off a Judging Category that
    already has scores against it"), naming the category and exactly how
    many cars were judged with it."""
    judged_count = judged_car_count_for_category(db, category)
    if judged_count > 0:
        subject = "1 car has" if judged_count == 1 else f"{judged_count} cars have"
        return (
            f"{subject} already been judged on {category.name}. Turning it off now would "
            "drop those scores from category totals and rankings, so this can't be changed for this show."
        )
    return None


def can_activate_category(db: Session, category: JudgingCategory) -> str | None:
    """Returns a plain-language reason it can't be turned on, or None if
    it's safe to. Blocks when ANY car in the show already has an
    accepted (judged) submission — those cars were scored without this
    category and nothing will retroactively fill it in."""
    has_judged_cars = (
        db.scalar(
            select(JudgingSubmission.id)
            .where(
                JudgingSubmission.show_id == category.show_id,
                JudgingSubmission.status == SubmissionStatus.ACCEPTED,
            )
            .limit(1)
        )
        is not None
    )
    if has_judged_cars:
        return (
            f"Cars have already been judged without {category.name}. Adding it now would "
            "leave those cars unscored in that category, so this can't be changed for this show."
        )
    return None
