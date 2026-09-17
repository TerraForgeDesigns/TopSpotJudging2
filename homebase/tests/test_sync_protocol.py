"""
POST /api/v1/sync — see PROTOCOL.md. Covers the request/response shape
(revision-gated configuration/cars, empty check-ins, the full-roster
summary) and the per-item validation/detail-application rules that
tests/test_sync_idempotency.py doesn't already cover. Idempotency
(retry -> already_recorded), cross-handheld duplicate -> flagged_duplicate,
and stale-range conversion are all exercised there already — not
repeated here.
"""
from datetime import date

import pytest
from sqlalchemy import select

from app.models import (
    Car,
    Handheld,
    JudgingCategory,
    JudgingSubmission,
    Show,
    VehicleCandidate,
    VehicleCandidateSighting,
)
from app.models.enums import CarStatus
from app.services.shows import set_active_show


@pytest.fixture()
def show_with_two_categories(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field", score_range_max=5)
    db_session.add(show)
    db_session.commit()
    set_active_show(db_session, show.id)
    engine = JudgingCategory(show_id=show.id, name="Engine", sort_order=0, active=True)
    paint = JudgingCategory(show_id=show.id, name="Paint", sort_order=1, active=True)
    db_session.add_all([engine, paint])
    car = Car(show_id=show.id, entry_number="001", data_revision_at_change=show.show_data_revision)
    db_session.add(car)
    db_session.commit()
    return show, engine, paint, car


def _sync(
    client,
    handheld_id="hh-1",
    config_revision=0,
    data_revision=0,
    submissions=None,
    battery_pct=80,
    known_car_count=0,
):
    payload = {
        "handheld_id": handheld_id,
        "config_revision": config_revision,
        "data_revision": data_revision,
        "known_car_count": known_car_count,
        "battery_pct": battery_pct,
        "submissions": submissions or [],
    }
    response = client.post("/api/v1/sync", json=payload)
    assert response.status_code == 200, response.text
    return response.json()


def _full_submission(entry_number, engine_id, paint_id, closed_at_uptime_ms=1000, score_range_max=5, **overrides):
    body = {
        "entry_number": entry_number,
        "closed_at_uptime_ms": closed_at_uptime_ms,
        "score_range_max": score_range_max,
        "scores": [{"category_id": engine_id, "points": 4}, {"category_id": paint_id, "points": 3}],
        "vehicle_photo_path": "/sdcard/topspot/photos/vehicle/test.jpg",
        "judge_sheet_photo_path": "/sdcard/topspot/photos/judge_sheets/test.jpg",
    }
    body.update(overrides)
    return body


# ---------------------------------------------------------------------
# Revision-gated configuration / cars
# ---------------------------------------------------------------------

def test_first_update_at_revision_zero_returns_full_configuration_and_cars(client, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories

    result = _sync(client, config_revision=0, data_revision=0)

    assert result["configuration"] is not None
    assert len(result["configuration"]["categories"]) == 2
    assert len(result["cars"]) == 1
    assert result["cars"][0]["entry_number"] == "001"
    assert result["sync_mode"] == "FULL"
    assert result["config_revision"] == show.configuration_revision
    assert result["data_revision"] == show.show_data_revision


def test_config_stale_only_returns_configuration_but_no_cars(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    # Handheld already has every car (data_revision current) but not the
    # latest Show Setup change.
    current_data_revision = show.show_data_revision

    result = _sync(client, config_revision=0, data_revision=current_data_revision, known_car_count=1)

    assert result["configuration"] is not None
    assert result["cars"] == []
    assert result["sync_mode"] == "DELTA"


def test_data_stale_only_returns_cars_but_no_configuration(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    current_config_revision = show.configuration_revision

    result = _sync(client, config_revision=current_config_revision, data_revision=0)

    assert result["configuration"] is None
    assert len(result["cars"]) == 1
    assert result["sync_mode"] == "FULL"


def test_both_current_returns_neither(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories

    result = _sync(
        client,
        config_revision=show.configuration_revision,
        data_revision=show.show_data_revision,
        known_car_count=1,
    )

    assert result["configuration"] is None
    assert result["cars"] == []
    assert result["sync_mode"] == "DELTA"


def test_incomplete_known_roster_forces_full_snapshot_even_when_revision_current(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    db_session.add(Car(show_id=show.id, entry_number="002", data_revision_at_change=show.show_data_revision))
    db_session.add(Car(show_id=show.id, entry_number="003", data_revision_at_change=show.show_data_revision))
    db_session.commit()

    result = _sync(
        client,
        config_revision=show.configuration_revision,
        data_revision=show.show_data_revision,
        known_car_count=1,
    )

    assert result["sync_mode"] == "FULL"
    assert [car["entry_number"] for car in result["cars"]] == ["001", "002", "003"]


def test_zero_known_roster_forces_full_300_car_snapshot_even_when_revision_current(
    client, db_session, show_with_two_categories
):
    show, engine, paint, car = show_with_two_categories
    for index in range(2, 301):
        db_session.add(Car(show_id=show.id, entry_number=f"{index:03d}", data_revision_at_change=show.show_data_revision))
    db_session.commit()

    result = _sync(
        client,
        config_revision=show.configuration_revision,
        data_revision=show.show_data_revision,
        known_car_count=0,
    )

    assert result["sync_mode"] == "FULL"
    assert len(result["cars"]) == 300
    assert result["cars"][0]["entry_number"] == "001"
    assert result["cars"][-1]["entry_number"] == "300"


def test_complete_known_roster_receives_three_car_delta_without_losing_unmentioned_entries(
    client, db_session, show_with_two_categories
):
    show, engine, paint, car = show_with_two_categories
    for index in range(2, 301):
        db_session.add(Car(show_id=show.id, entry_number=f"{index:03d}", data_revision_at_change=show.show_data_revision))
    db_session.commit()
    before_revision = show.show_data_revision

    for entry in ("016", "021", "022"):
        target = db_session.scalars(select(Car).where(Car.show_id == show.id, Car.entry_number == entry)).one()
        target.status = CarStatus.JUDGED
        show.show_data_revision += 1
        target.data_revision_at_change = show.show_data_revision
    db_session.commit()

    result = _sync(
        client,
        config_revision=show.configuration_revision,
        data_revision=before_revision,
        known_car_count=300,
    )

    assert result["sync_mode"] == "DELTA"
    assert [car["entry_number"] for car in result["cars"]] == ["016", "021", "022"]
    assert all(car["status"] == "judged" for car in result["cars"])


def test_empty_check_in_succeeds_and_still_returns_summary_and_server_time(client, show_with_two_categories):
    """The normal case for most updates — see PROTOCOL.md: a handheld
    with nothing queued still calls this to refresh."""
    result = _sync(client, submissions=[])

    assert result["results"] == []
    assert result["summary"]["total_cars"] == 1
    assert "server_time" in result
    assert "config_revision" in result and "data_revision" in result


def test_sync_updates_handheld_row(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories

    _sync(client, handheld_id="hh-9", config_revision=3, data_revision=7, battery_pct=42)

    handheld = db_session.scalars(select(Handheld).where(Handheld.label == "hh-9")).first()
    assert handheld is not None
    assert handheld.battery_pct == 42
    assert handheld.last_config_revision == 3
    assert handheld.last_data_revision == 7
    assert handheld.last_sync_at is not None


# ---------------------------------------------------------------------
# Per-item validation
# ---------------------------------------------------------------------

def test_missing_category_score_is_rejected(client, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = {
        "entry_number": "001",
        "closed_at_uptime_ms": 1000,
        "score_range_max": 5,
        "scores": [{"category_id": engine.id, "points": 4}],  # Paint missing
        "vehicle_photo_path": "/sdcard/topspot/photos/vehicle/test.jpg",
        "judge_sheet_photo_path": "/sdcard/topspot/photos/judge_sheets/test.jpg",
    }

    result = _sync(client, submissions=[submission])

    assert result["results"][0]["status"] == "error"
    assert "Paint" in result["results"][0]["message"]
    assert "has not been scored" in result["results"][0]["message"]


def test_out_of_range_score_is_rejected(client, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = _full_submission("001", engine.id, paint.id, score_range_max=5)
    submission["scores"][1]["points"] = 12  # Paint, out of 1-5

    result = _sync(client, submissions=[submission])

    assert result["results"][0]["status"] == "error"
    assert "Paint" in result["results"][0]["message"]
    assert "outside the allowed range" in result["results"][0]["message"]


def test_zero_score_is_rejected_not_treated_as_not_applicable(client, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = _full_submission("001", engine.id, paint.id)
    submission["scores"][0]["points"] = 0  # Engine

    result = _sync(client, submissions=[submission])

    assert result["results"][0]["status"] == "error"


def test_unknown_entry_number_is_rejected(client, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = _full_submission("999", engine.id, paint.id)

    result = _sync(client, submissions=[submission])

    assert result["results"][0]["status"] == "error"
    assert "999" in result["results"][0]["message"]


def test_invalid_submission_writes_nothing(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = {
        "entry_number": "001",
        "closed_at_uptime_ms": 1000,
        "score_range_max": 5,
        "scores": [{"category_id": engine.id, "points": 4}],  # Paint missing
    }

    _sync(client, submissions=[submission])

    assert db_session.scalars(select(JudgingSubmission)).first() is None


# ---------------------------------------------------------------------
# Entry details — fill blanks, correct differing values
# ---------------------------------------------------------------------

def test_details_filled_in_by_judge_on_a_blank_entry(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = _full_submission(
        "001", engine.id, paint.id,
        participant="R. Alvarez", year="1969", make="Chevrolet", model="Camaro", vehicle_type="Car",
    )

    _sync(client, submissions=[submission])

    refreshed = db_session.get(Car, car.id)
    assert refreshed.participant == "R. Alvarez"
    assert refreshed.year == "1969"
    assert refreshed.make == "Chevrolet"
    assert refreshed.model == "Camaro"
    assert refreshed.vehicle_type == "Car"


def test_non_empty_differing_value_overwrites_and_is_treated_as_a_correction(client, db_session, show_with_two_categories, caplog):
    show, engine, paint, car = show_with_two_categories
    car.participant = "Wrong Name"
    db_session.commit()

    submission = _full_submission(
        "001", engine.id, paint.id, closed_at_uptime_ms=2000, participant="R. Alvarez",
    )
    import logging

    with caplog.at_level(logging.INFO, logger="app.services.sync"):
        _sync(client, submissions=[submission])

    refreshed = db_session.get(Car, car.id)
    assert refreshed.participant == "R. Alvarez"
    assert any("corrected" in message for message in caplog.messages)


def test_blank_incoming_value_never_clears_an_existing_one(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    car.participant = "R. Alvarez"
    db_session.commit()

    submission = _full_submission("001", engine.id, paint.id)  # no participant field sent (None)

    _sync(client, submissions=[submission])

    refreshed = db_session.get(Car, car.id)
    assert refreshed.participant == "R. Alvarez"  # untouched


# ---------------------------------------------------------------------
# New Vehicle Names candidates
# ---------------------------------------------------------------------

def test_manual_make_creates_a_vehicle_candidate_sighting(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = _full_submission(
        "001", engine.id, paint.id, make="Studebaker", model="Champion",
        make_manually_entered=True, model_manually_entered=False,
    )

    _sync(client, submissions=[submission])

    candidate = db_session.scalars(select(VehicleCandidate)).first()
    assert candidate is not None
    assert candidate.make_name == "Studebaker"
    assert candidate.model_name == "Champion"
    assert candidate.times_seen == 1

    sighting = db_session.scalars(select(VehicleCandidateSighting)).first()
    assert sighting is not None
    assert sighting.candidate_id == candidate.id
    assert sighting.entry_id == car.id
    assert sighting.show_id == show.id


def test_manual_make_seen_twice_increments_times_seen_not_a_new_row(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    second_car = Car(show_id=show.id, entry_number="002", data_revision_at_change=show.show_data_revision)
    db_session.add(second_car)
    db_session.commit()

    _sync(client, submissions=[_full_submission(
        "001", engine.id, paint.id, closed_at_uptime_ms=1000, make="Studebaker", model="Champion",
        make_manually_entered=True,
    )])
    _sync(client, submissions=[_full_submission(
        "002", engine.id, paint.id, closed_at_uptime_ms=1000, make="Studebaker", model="Champion",
        make_manually_entered=True,
    )])

    candidates = list(db_session.scalars(select(VehicleCandidate)))
    assert len(candidates) == 1
    assert candidates[0].times_seen == 2
    assert len(list(db_session.scalars(select(VehicleCandidateSighting)))) == 2


def test_no_candidate_recorded_when_neither_flag_is_manually_entered(client, db_session, show_with_two_categories):
    show, engine, paint, car = show_with_two_categories
    submission = _full_submission("001", engine.id, paint.id, make="Chevrolet", model="Camaro")

    _sync(client, submissions=[submission])

    assert db_session.scalars(select(VehicleCandidate)).first() is None
