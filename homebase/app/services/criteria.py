"""
Judging Criteria CRUD, reordering, and activate/deactivate.

Deactivating a criterion must NEVER touch JudgingScore rows already
submitted against it — those are historical record and stay visible in
results. Deactivating only removes the criterion from *future* judging
(handhelds only pull `active` criteria — see PROTOCOL.md roster sync).
There is deliberately no delete operation here: once a criterion has ever
been scored against, removing it would orphan JudgingScore rows, so
deactivate is the only retirement path.
"""
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import JudgingCriteria


def list_criteria(db: Session, show_id: int) -> list[JudgingCriteria]:
    return list(
        db.scalars(
            select(JudgingCriteria).where(JudgingCriteria.show_id == show_id).order_by(JudgingCriteria.sort_order)
        )
    )


def create_criteria(db: Session, show_id: int, name: str, max_points: int) -> JudgingCriteria:
    max_order = db.scalar(
        select(JudgingCriteria.sort_order)
        .where(JudgingCriteria.show_id == show_id)
        .order_by(JudgingCriteria.sort_order.desc())
    )
    criteria = JudgingCriteria(
        show_id=show_id, name=name.strip(), max_points=max_points, sort_order=(max_order or 0) + 1
    )
    db.add(criteria)
    db.commit()
    return criteria


def update_criteria(db: Session, criteria_id: int, name: str, max_points: int) -> JudgingCriteria:
    criteria = db.get(JudgingCriteria, criteria_id)
    if criteria is None:
        raise ValueError(f"No judging criteria with id {criteria_id}")
    criteria.name = name.strip()
    criteria.max_points = max_points
    db.commit()
    return criteria


def set_active(db: Session, criteria_id: int, active: bool) -> JudgingCriteria:
    criteria = db.get(JudgingCriteria, criteria_id)
    if criteria is None:
        raise ValueError(f"No judging criteria with id {criteria_id}")
    criteria.active = active
    db.commit()
    return criteria


def move_criteria(db: Session, show_id: int, criteria_id: int, direction: str) -> None:
    items = list(
        db.scalars(
            select(JudgingCriteria).where(JudgingCriteria.show_id == show_id).order_by(JudgingCriteria.sort_order)
        )
    )
    idx = next((i for i, c in enumerate(items) if c.id == criteria_id), None)
    if idx is None:
        return
    swap_idx = idx - 1 if direction == "up" else idx + 1
    if swap_idx < 0 or swap_idx >= len(items):
        return
    items[idx].sort_order, items[swap_idx].sort_order = items[swap_idx].sort_order, items[idx].sort_order
    db.commit()
