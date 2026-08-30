"""
Car roster CRUD. Bulk CSV import lives in car_import.py — this module is
single-car add/edit/delete plus the roster listing query.
"""
from dataclasses import dataclass

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import Car


class DuplicateRegistrationNumber(ValueError):
    def __init__(self, registration_number: str):
        super().__init__(f"Registration number '{registration_number}' is already in use.")
        self.registration_number = registration_number


def list_cars(db: Session, show_id: int) -> list[Car]:
    return list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show_id)
            .options(selectinload(Car.car_class))
            .order_by(Car.registration_number)
        )
    )


def get_car(db: Session, car_id: int) -> Car | None:
    return db.get(Car, car_id)


def _check_unique_registration(db: Session, registration_number: str, exclude_car_id: int | None = None) -> None:
    stmt = select(Car).where(Car.registration_number == registration_number)
    if exclude_car_id is not None:
        stmt = stmt.where(Car.id != exclude_car_id)
    existing = db.scalars(stmt).first()
    if existing is not None:
        raise DuplicateRegistrationNumber(registration_number)


def create_car(
    db: Session,
    show_id: int,
    registration_number: str,
    display_car_number: str,
    make: str,
    model: str,
    year: int,
    class_id: int | None,
) -> Car:
    registration_number = registration_number.strip()
    _check_unique_registration(db, registration_number)

    car = Car(
        show_id=show_id,
        registration_number=registration_number,
        display_car_number=display_car_number.strip(),
        make=make.strip(),
        model=model.strip(),
        year=year,
        class_id=class_id,
    )
    db.add(car)
    db.commit()
    db.refresh(car)
    return car


def update_car(
    db: Session,
    car_id: int,
    registration_number: str,
    display_car_number: str,
    make: str,
    model: str,
    year: int,
    class_id: int | None,
    owner_name: str | None = None,
    announcer_name: str | None = None,
) -> Car:
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")

    registration_number = registration_number.strip()
    _check_unique_registration(db, registration_number, exclude_car_id=car_id)

    car.registration_number = registration_number
    car.display_car_number = display_car_number.strip()
    car.make = make.strip()
    car.model = model.strip()
    car.year = year
    car.class_id = class_id
    car.owner_name = (owner_name or "").strip() or None
    car.announcer_name = (announcer_name or "").strip() or None
    db.commit()
    db.refresh(car)
    return car


@dataclass
class JudgingDataSummary:
    submission_count: int
    photo_count: int

    @property
    def has_data(self) -> bool:
        return self.submission_count > 0 or self.photo_count > 0


def judging_data_summary(db: Session, car_id: int) -> JudgingDataSummary:
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")
    return JudgingDataSummary(submission_count=len(car.submissions), photo_count=len(car.photos))


def delete_car(db: Session, car_id: int) -> None:
    """Cascades to JudgingSubmission (and its JudgingScore rows) and Photo
    via the ORM relationship cascade on Car — see models/car.py."""
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")
    db.delete(car)
    db.commit()
