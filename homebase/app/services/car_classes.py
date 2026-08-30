"""
Car Class CRUD + reordering. See CONTEXT.md glossary — a class is an award
grouping, independent of the judging rubric. Deleting one must never orphan
or cascade-delete the cars in it: they fall back to "Unclassified"
(class_id = NULL), which the UI renders literally as that label.
"""
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import Car, CarClass


def list_classes_with_counts(db: Session, show_id: int) -> list[tuple[CarClass, int]]:
    classes = list(
        db.scalars(select(CarClass).where(CarClass.show_id == show_id).order_by(CarClass.sort_order))
    )
    counts = {c.id: 0 for c in classes}
    for (class_id,) in db.execute(
        select(Car.class_id).where(Car.show_id == show_id, Car.class_id.is_not(None))
    ):
        counts[class_id] = counts.get(class_id, 0) + 1
    return [(c, counts.get(c.id, 0)) for c in classes]


def create_class(db: Session, show_id: int, name: str) -> CarClass:
    max_order = db.scalar(
        select(CarClass.sort_order).where(CarClass.show_id == show_id).order_by(CarClass.sort_order.desc())
    )
    car_class = CarClass(show_id=show_id, name=name.strip(), sort_order=(max_order or 0) + 1)
    db.add(car_class)
    db.commit()
    return car_class


def rename_class(db: Session, class_id: int, name: str) -> CarClass:
    car_class = db.get(CarClass, class_id)
    if car_class is None:
        raise ValueError(f"No car class with id {class_id}")
    car_class.name = name.strip()
    db.commit()
    return car_class


def move_class(db: Session, show_id: int, class_id: int, direction: str) -> None:
    classes = list(
        db.scalars(select(CarClass).where(CarClass.show_id == show_id).order_by(CarClass.sort_order))
    )
    idx = next((i for i, c in enumerate(classes) if c.id == class_id), None)
    if idx is None:
        return
    swap_idx = idx - 1 if direction == "up" else idx + 1
    if swap_idx < 0 or swap_idx >= len(classes):
        return
    classes[idx].sort_order, classes[swap_idx].sort_order = (
        classes[swap_idx].sort_order,
        classes[idx].sort_order,
    )
    db.commit()


def delete_class(db: Session, class_id: int) -> int:
    """Reassigns every car in this class to Unclassified (class_id=NULL),
    then deletes the class. Returns the number of cars reassigned."""
    car_class = db.get(CarClass, class_id)
    if car_class is None:
        raise ValueError(f"No car class with id {class_id}")

    affected_cars = list(db.scalars(select(Car).where(Car.class_id == class_id)))
    for car in affected_cars:
        car.class_id = None

    db.delete(car_class)
    db.commit()
    return len(affected_cars)
