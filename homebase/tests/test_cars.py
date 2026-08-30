"""
Post-creation Car (Entry) management — see CONTEXT.md's Cars section
and Edit Show's Add Cars section.
"""
from datetime import date, datetime, timezone

import pytest
from sqlalchemy import select

from app.models import (
    Car,
    CarStatus,
    Handheld,
    JudgingCategory,
    JudgingScore,
    JudgingSubmission,
    Photo,
    PhotoStatus,
    PhotoType,
    Show,
    SubmissionStatus,
    TransferMethod,
)
from app.services.cars import add_cars, get_car, list_cars, update_car_details


@pytest.fixture()
def show(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field", score_range_max=5)
    db_session.add(show)
    db_session.commit()
    return show


def test_list_cars_blank_state_for_unfilled_entries(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()

    rows = list_cars(db_session, show.id)
    assert len(rows) == 1
    assert rows[0].car.participant is None  # the template renders this as "—", never "None"
    assert rows[0].score is None  # not a phantom 0


def test_list_cars_includes_score_for_judged_car(db_session, show):
    category = JudgingCategory(show_id=show.id, name="Engine", sort_order=0, active=True)
    handheld = Handheld(label="hh-1")
    db_session.add_all([category, handheld])
    db_session.commit()

    car = Car(show_id=show.id, entry_number="001", status=CarStatus.JUDGED, data_revision_at_change=1)
    db_session.add(car)
    db_session.flush()
    submission = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number="001", handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=0, status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.flush()
    db_session.add(JudgingScore(submission_id=submission.id, category_id=category.id, original_points=4, original_range_max=5, adjusted_points=4))
    db_session.commit()

    rows = list_cars(db_session, show.id)
    assert rows[0].score == 4


def test_list_cars_photo_status_defaults_to_neither(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()

    rows = list_cars(db_session, show.id)
    assert rows[0].has_car_photo is False
    assert rows[0].has_judge_sheet_photo is False


def test_list_cars_photo_status_reflects_matched_photos(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.flush()
    db_session.add(
        Photo(
            show_id=show.id, car_id=car.id, entry_number="001", photo_type=PhotoType.CAR,
            status=PhotoStatus.MATCHED, transfer_method=TransferMethod.SD_CARD,
            file_path="x/car.jpg", original_filename="001_car.jpg",
        )
    )
    db_session.commit()

    rows = list_cars(db_session, show.id)
    assert rows[0].has_car_photo is True
    assert rows[0].has_judge_sheet_photo is False


def test_list_cars_photo_status_counts_a_duplicate_as_present(db_session, show):
    """A DUPLICATE still means "something usable exists" for the Cars
    table's findability purpose — it just also needs the host to resolve
    which copy is canonical, tracked separately on /photos."""
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.flush()
    db_session.add(
        Photo(
            show_id=show.id, car_id=car.id, entry_number="001", photo_type=PhotoType.JUDGE_SHEET,
            status=PhotoStatus.DUPLICATE, transfer_method=TransferMethod.SD_CARD,
            file_path="x/judge_sheet_duplicate.jpg", original_filename="001_sheet.jpg",
        )
    )
    db_session.commit()

    rows = list_cars(db_session, show.id)
    assert rows[0].has_judge_sheet_photo is True


def test_list_cars_photo_status_ignores_unmatched_photos_of_other_cars(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()
    # An unmatched photo has car_id=None — must never be attributed to any row.
    db_session.add(
        Photo(
            show_id=show.id, car_id=None, entry_number="999", photo_type=PhotoType.CAR,
            status=PhotoStatus.UNMATCHED, transfer_method=TransferMethod.SD_CARD,
            file_path="x/unmatched.jpg", original_filename="999_car.jpg",
        )
    )
    db_session.commit()

    rows = list_cars(db_session, show.id)
    assert rows[0].has_car_photo is False


def test_update_car_details_never_touches_entry_number(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()

    update_car_details(db_session, show, car, "R. Alvarez", "1969", "Chevrolet", "Camaro", "Car")

    refreshed = get_car(db_session, car.id)
    assert refreshed.entry_number == "001"
    assert refreshed.participant == "R. Alvarez"
    assert refreshed.year == "1969"
    assert refreshed.make == "Chevrolet"
    assert refreshed.model == "Camaro"
    assert refreshed.vehicle_type == "Car"


def test_update_car_details_bumps_show_data_revision_and_stamps_the_entry(db_session, show):
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()
    revision_before = show.show_data_revision

    update_car_details(db_session, show, car, "R. Alvarez", "1969", "Chevrolet", "Camaro", "Car")

    assert show.show_data_revision == revision_before + 1
    assert car.data_revision_at_change == show.show_data_revision


def test_update_car_details_blank_fields_clear_to_none_not_empty_string(db_session, show):
    car = Car(show_id=show.id, entry_number="001", participant="Old Name", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()

    update_car_details(db_session, show, car, "", "", "", "", "")

    refreshed = get_car(db_session, car.id)
    assert refreshed.participant is None


# ---------------------------------------------------------------------
# Add Cars
# ---------------------------------------------------------------------

def test_add_cars_assigns_next_available_numbers(db_session, show):
    for i in range(1, 11):
        db_session.add(Car(show_id=show.id, entry_number=f"{i:03d}", data_revision_at_change=1))
    db_session.commit()

    result = add_cars(db_session, show, 5)

    assert result.added_count == 5
    assert result.first_entry_number == "011"
    assert result.last_entry_number == "015"
    new_numbers = sorted(
        c.entry_number for c in db_session.scalars(select(Car).where(Car.show_id == show.id)).all()
    )
    assert new_numbers == [f"{i:03d}" for i in range(1, 16)]


def test_add_cars_never_renumbers_existing_entries_or_touches_scores(db_session, show):
    category = JudgingCategory(show_id=show.id, name="Engine", sort_order=0, active=True)
    handheld = Handheld(label="hh-1")
    db_session.add_all([category, handheld])
    db_session.commit()

    car = Car(show_id=show.id, entry_number="001", status=CarStatus.JUDGED, data_revision_at_change=1)
    db_session.add(car)
    db_session.flush()
    submission = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number="001", handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=0, status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.flush()
    score = JudgingScore(submission_id=submission.id, category_id=category.id, original_points=4, original_range_max=5, adjusted_points=4)
    db_session.add(score)
    db_session.commit()
    score_id = score.id

    add_cars(db_session, show, 5)

    refreshed_car = get_car(db_session, car.id)
    assert refreshed_car.entry_number == "001"  # never renumbered
    refreshed_score = db_session.get(JudgingScore, score_id)
    assert refreshed_score.adjusted_points == 4  # untouched


def test_add_cars_bumps_show_data_revision_once(db_session, show):
    revision_before = show.show_data_revision
    add_cars(db_session, show, 10)
    assert show.show_data_revision == revision_before + 1


def test_add_cars_crossing_a_tier_escalates_and_converts_existing_scores(db_session, show):
    category = JudgingCategory(show_id=show.id, name="Engine", sort_order=0, active=True)
    handheld = Handheld(label="hh-1")
    db_session.add_all([category, handheld])
    db_session.commit()

    for i in range(1, 146):
        db_session.add(Car(show_id=show.id, entry_number=f"{i:03d}", data_revision_at_change=1))
    db_session.commit()
    judged_car = db_session.scalars(select(Car).where(Car.show_id == show.id, Car.entry_number == "001")).first()
    judged_car.status = CarStatus.JUDGED
    db_session.flush()
    submission = JudgingSubmission(
        show_id=show.id, car_id=judged_car.id, entry_number="001", handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=0, status=SubmissionStatus.ACCEPTED,
    )
    db_session.add(submission)
    db_session.flush()
    score = JudgingScore(submission_id=submission.id, category_id=category.id, original_points=4, original_range_max=5, adjusted_points=4)
    db_session.add(score)
    db_session.commit()

    assert show.score_range_max == 5

    result = add_cars(db_session, show, 10)  # 145 -> 155, crosses 150

    assert result.escalated is True
    assert result.new_score_range_max == 10
    assert show.score_range_max == 10
    refreshed_score = db_session.scalars(select(JudgingScore)).first()
    assert refreshed_score.original_points == 4  # untouched
    assert refreshed_score.adjusted_points == 8  # 4 * 10/5


def test_add_cars_below_threshold_does_not_escalate(db_session, show):
    for i in range(1, 11):
        db_session.add(Car(show_id=show.id, entry_number=f"{i:03d}", data_revision_at_change=1))
    db_session.commit()

    result = add_cars(db_session, show, 5)
    assert result.escalated is False
    assert show.score_range_max == 5
