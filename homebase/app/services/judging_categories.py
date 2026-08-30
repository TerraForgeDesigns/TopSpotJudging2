"""
Post-creation Judging Category management — see CONTEXT.md's Edit Show
"Judging Setup" section. The protection RULE lives in
services/judging_category_rules.py as a checkable, independently-tested
function; this module is where it's actually enforced against real (not
draft) JudgingCategory rows. Renaming and reordering have no guard —
CONTEXT.md is explicit that both stay allowed regardless of judging
state. Every mutation bumps configuration_revision via
services/revisions.py — never a direct column write.
"""
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import JudgingCategory, Show
from app.services.judging_category_rules import can_activate_category, can_deactivate_category
from app.services.revisions import bump_configuration_revision


def list_categories(db: Session, show_id: int) -> list[JudgingCategory]:
    return list(
        db.scalars(
            select(JudgingCategory).where(JudgingCategory.show_id == show_id).order_by(JudgingCategory.sort_order)
        )
    )


def get_category(db: Session, category_id: int) -> JudgingCategory | None:
    return db.get(JudgingCategory, category_id)


def toggle_category(db: Session, show: Show, category: JudgingCategory) -> str | None:
    """Returns a plain-language reason the toggle was refused, or None on
    success (and the toggle already happened). See
    services/judging_category_rules.py for the rule itself — this
    function only decides which direction to check and applies it."""
    blocked = can_deactivate_category(db, category) if category.active else can_activate_category(db, category)
    if blocked:
        return blocked
    category.active = not category.active
    bump_configuration_revision(db, show)
    db.commit()
    return None


def rename_category(db: Session, show: Show, category: JudgingCategory, name: str) -> None:
    """Always allowed, regardless of judging state — CONTEXT.md."""
    name = (name or "").strip()
    if not name:
        return
    category.name = name
    bump_configuration_revision(db, show)
    db.commit()


def reorder_categories(db: Session, show: Show, ordered_ids: list[int]) -> None:
    """Always allowed, regardless of judging state — CONTEXT.md. This is
    the SAME field as display order (see models/category.py's docstring
    and DECISIONS.md's tiebreak_priority decision) — reordering here
    changes both what judges see on the handheld and tie-break priority,
    which the Edit Show template must say plainly, same as the wizard."""
    order_index = {cat_id: i for i, cat_id in enumerate(ordered_ids)}
    categories = list(db.scalars(select(JudgingCategory).where(JudgingCategory.show_id == show.id)))
    for category in categories:
        if category.id in order_index:
            category.sort_order = order_index[category.id]
    bump_configuration_revision(db, show)
    db.commit()


def toggle_overall_impression(db: Session, show: Show) -> None:
    show.overall_impression_enabled = not show.overall_impression_enabled
    bump_configuration_revision(db, show)
    db.commit()
