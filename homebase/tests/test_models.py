import time
from datetime import date, datetime, timezone

import pytest
from sqlalchemy.exc import IntegrityError

from app.models import (
    Car,
    CarClass,
    CarStatus,
    Handheld,
    JudgingCriteria,
    JudgingScore,
    JudgingSubmission,
    Photo,
    PhotoStatus,
    PhotoType,
    Show,
    SubmissionStatus,
    TransferMethod,
)


def _make_show(db_session) -> Show:
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 12), location="Downtown Lot")
    db_session.add(show)
    db_session.commit()
    return show


def test_car_defaults_on_create(db_session):
    show = _make_show(db_session)
    car = Car(
        show_id=show.id,
        registration_number="0142",
        display_car_number="142",
        make="Chevrolet",
        model="Camaro",
        year=1967,
    )
    db_session.add(car)
    db_session.commit()

    assert car.status == CarStatus.UNJUDGED
    assert car.created_at is not None
    assert car.updated_at is not None
    assert car.owner_name is None
    assert car.class_id is None


def test_car_updated_at_changes_on_update(db_session):
    show = _make_show(db_session)
    car = Car(
        show_id=show.id,
        registration_number="0142",
        display_car_number="142",
        make="Chevrolet",
        model="Camaro",
        year=1967,
    )
    db_session.add(car)
    db_session.commit()

    original_updated_at = car.updated_at
    time.sleep(0.01)

    car.owner_name = "R. Alvarez"
    db_session.commit()

    assert car.updated_at > original_updated_at


def test_judging_criteria_updated_at_changes_on_update(db_session):
    show = _make_show(db_session)
    criteria = JudgingCriteria(show_id=show.id, name="Paint", max_points=25, sort_order=1)
    db_session.add(criteria)
    db_session.commit()

    original_updated_at = criteria.updated_at
    time.sleep(0.01)

    criteria.max_points = 30
    db_session.commit()

    assert criteria.updated_at > original_updated_at


def test_registration_number_is_unique(db_session):
    show = _make_show(db_session)
    db_session.add(
        Car(
            show_id=show.id,
            registration_number="0142",
            display_car_number="142",
            make="Chevrolet",
            model="Camaro",
            year=1967,
        )
    )
    db_session.commit()

    db_session.add(
        Car(
            show_id=show.id,
            registration_number="0142",
            display_car_number="999",
            make="Ford",
            model="Mustang",
            year=1965,
        )
    )
    with pytest.raises(IntegrityError):
        db_session.commit()


def test_full_judging_chain(db_session):
    show = _make_show(db_session)
    car_class = CarClass(show_id=show.id, name="1970s Muscle", sort_order=1)
    criteria = JudgingCriteria(show_id=show.id, name="Paint", max_points=25, sort_order=1)
    handheld = Handheld(label="Judge 2")
    db_session.add_all([car_class, criteria, handheld])
    db_session.commit()

    car = Car(
        show_id=show.id,
        class_id=car_class.id,
        registration_number="0142",
        display_car_number="142",
        make="Chevrolet",
        model="Camaro",
        year=1967,
    )
    db_session.add(car)
    db_session.commit()

    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        judge_name="R. Alvarez",
        closed_at=datetime.now(timezone.utc),
        status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.commit()

    score = JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=22)
    photo = Photo(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        photo_type=PhotoType.CAR,
        status=PhotoStatus.MATCHED,
        source_handheld_id=handheld.id,
        transfer_method=TransferMethod.USB,
        file_path="photos/0142_car.jpg",
        original_filename="0142_car.jpg",
    )
    db_session.add_all([score, photo])
    db_session.commit()

    assert car.car_class.name == "1970s Muscle"
    assert car.submissions[0].scores[0].points == 22
    assert car.photos[0].file_path.endswith("_car.jpg")
    assert handheld.submissions[0].car.registration_number == "0142"
