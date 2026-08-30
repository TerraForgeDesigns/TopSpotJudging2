"""
Car (Entry) management post-creation — see CONTEXT.md's Cars section and
Edit Show's Add Cars section. Every mutation here goes through
services/revisions.py — never a direct write to
show_data_revision/configuration_revision — and every entry change
stamps Car.data_revision_at_change so PROTOCOL.md's delta sync picks it
up on the handhelds' next update.

Entry Number is never accepted as an editable field anywhere in this
module — see CONTEXT.md's "Editing an entry" note and update_car_details().
"""
from dataclasses import dataclass

from sqlalchemy import func, select
from sqlalchemy.orm import Session, selectinload

from app.models import Car, JudgingSubmission, Show
from app.services.range_escalation import check_and_escalate
from app.services.revisions import bump_show_data_revision
from app.services.scoring import get_accepted_submission, submission_total


@dataclass
class CarRow:
    car: Car
    score: int | None  # None means not yet judged — never a phantom 0


def list_cars(db: Session, show_id: int) -> list[CarRow]:
    cars = list(
        db.scalars(
            select(Car)
            .where(Car.show_id == show_id)
            .options(selectinload(Car.submissions).selectinload(JudgingSubmission.scores))
            .order_by(Car.entry_number)
        )
    )
    rows = []
    for car in cars:
        submission = get_accepted_submission(car)
        rows.append(CarRow(car=car, score=submission_total(submission) if submission else None))
    return rows


def get_car(db: Session, car_id: int) -> Car | None:
    return db.get(Car, car_id)


def update_car_details(
    db: Session,
    show: Show,
    car: Car,
    participant: str,
    year: str,
    make: str,
    model: str,
    vehicle_type: str,
) -> None:
    """The host fills in or corrects these at any time — see CONTEXT.md's
    "The real-world judging workflow." Entry Number is deliberately not a
    parameter here at all; there is no path through this function that
    can change it."""
    car.participant = participant.strip() or None
    car.year = year.strip() or None
    car.make = make.strip() or None
    car.model = model.strip() or None
    car.vehicle_type = vehicle_type.strip() or None
    bump_show_data_revision(db, show)
    car.data_revision_at_change = show.show_data_revision
    db.commit()


@dataclass
class AddCarsResult:
    added_count: int
    first_entry_number: str
    last_entry_number: str
    escalated: bool
    new_score_range_max: int


def add_cars(db: Session, show: Show, count: int) -> AddCarsResult:
    """Creates `count` new entries at the next available numbers — never
    renumbers existing entries, never touches existing scores (CONTEXT.md).
    The car-adds, the show_data_revision bump, and the range-escalation
    check all happen in this one function and commit together as a single
    transaction — see services/range_escalation.py's docstring for why
    check_and_escalate() deliberately doesn't commit on its own; this is
    exactly the composed operation that requires that."""
    existing_count = db.scalar(select(func.count(Car.id)).where(Car.show_id == show.id)) or 0
    first = existing_count + 1
    last = existing_count + count

    bump_show_data_revision(db, show)
    for i in range(first, last + 1):
        db.add(Car(show_id=show.id, entry_number=f"{i:03d}", data_revision_at_change=show.show_data_revision))

    escalated = check_and_escalate(db, show)
    db.commit()

    return AddCarsResult(
        added_count=count,
        first_entry_number=f"{first:03d}",
        last_entry_number=f"{last:03d}",
        escalated=escalated,
        new_score_range_max=show.score_range_max,
    )
