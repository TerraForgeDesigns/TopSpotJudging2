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

Neither direction has a caller yet — post-creation Judging Category
management is not built (see the /criteria stub, DECISIONS.md). These
functions exist now so that whenever that management screen is built,
the rule is already correct and tested, not bolted on after the fact.
Pure query + message functions: no mutation, no commit — a caller checks
`can_deactivate_category`/`can_activate_category` before flipping
`JudgingCategory.active` itself.
"""
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import JudgingCategory, JudgingScore, JudgingSubmission, SubmissionStatus


def can_deactivate_category(db: Session, category: JudgingCategory) -> str | None:
    """Returns a plain-language reason it can't be turned off, or None if
    it's safe to. Blocks only when the category already has scores —
    matches CONTEXT.md exactly ("Turning off a Judging Category that
    already has scores against it")."""
    has_scores = (
        db.scalar(select(JudgingScore.id).where(JudgingScore.category_id == category.id).limit(1)) is not None
    )
    if has_scores:
        return (
            f"Cars have already been judged on {category.name}. Turning it off now would "
            "drop those scores from category totals and rankings."
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
            "leave those cars unscored in that category."
        )
    return None
