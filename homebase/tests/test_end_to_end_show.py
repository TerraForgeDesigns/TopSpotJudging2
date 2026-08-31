"""
INT1 item 1 — a headless regression suite driven entirely through
SimulatedHandheld (see handheld_simulator.py), covering every scenario
that task named: fetching show info, sending scores and nominations,
going out of range, retrying, a duplicate from another device, resending
after a lost acknowledgement, an unknown entry number, filling in
participant details, and manually entered makes/models. Each scenario is
its own small, focused test rather than one giant one.
"""
from sqlalchemy import select

from app.models import VehicleCandidate
from tests.factories import build_show
from tests.handheld_simulator import SimulatedHandheld


def _show(db_session, **overrides):
    return build_show(db_session, car_count=5, **overrides)


def test_fetching_show_info(client, db_session):
    show = _show(db_session)

    hh = SimulatedHandheld(client, "hh-1")
    response = hh.check_in()

    assert response["configuration"]["show_name"] == show.name
    assert len(response["configuration"]["categories"]) == 5  # all 5 built-ins active by default
    assert len(response["cars"]) == 5
    assert hh.roster["001"]["status"] == "unjudged"


def test_sending_scores_and_nominations(client, db_session):
    _show(db_session, top_awards_count=10)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()

    response = hh.judge_car(
        "001",
        {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3},
        participant="R. Alvarez",
        make="Chevrolet",
        model="Camaro",
        nominations=["Best Paint", "Best Car"],
    )

    assert response["results"][0]["status"] == "accepted"
    assert hh.queue == []
    assert hh.roster["001"]["status"] == "judged"
    assert hh.roster["001"]["participant"] == "R. Alvarez"


def test_going_out_of_range_keeps_the_queue_intact(client, db_session):
    _show(db_session)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()

    hh.go_out_of_range()
    result = hh.judge_car("001", {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3})

    assert result is None  # no network call at all while out of range
    assert len(hh.queue) == 1  # nothing queued is ever dropped on a miss


def test_coming_back_into_range_drains_the_queue(client, db_session):
    _show(db_session)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()
    hh.go_out_of_range()
    hh.judge_car("001", {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3})
    assert len(hh.queue) == 1

    hh.come_back_into_range()
    response = hh.check_in()

    assert response["results"][0]["status"] == "accepted"
    assert hh.queue == []


def test_a_duplicate_from_another_handheld_is_flagged_and_the_original_is_untouched(client, db_session):
    _show(db_session)
    hh1 = SimulatedHandheld(client, "hh-1")
    hh2 = SimulatedHandheld(client, "hh-2")
    hh1.check_in()
    hh2.check_in()

    first = hh1.judge_car("001", {"Engine": 3, "Exterior": 3, "Interior": 3, "Paint": 3, "Wheels / Tires": 3})
    assert first["results"][0]["status"] == "accepted"

    second = hh2.judge_car("001", {"Engine": 5, "Exterior": 5, "Interior": 5, "Paint": 5, "Wheels / Tires": 5})
    assert second["results"][0]["status"] == "flagged_duplicate"
    assert hh2.queue == []  # lost the fight — nothing left to retry
    assert hh2.roster["001"]["status"] == "flagged_conflict"


def test_resending_after_a_lost_acknowledgement(client, db_session):
    _show(db_session)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()

    raw = hh.judge_car_losing_the_ack("001", {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3})
    assert raw["results"][0]["status"] == "accepted"  # the server really did record it
    assert len(hh.queue) == 1  # but the handheld never found out

    # A later trigger resends the identical triple.
    response = hh.check_in()
    assert response["results"][0]["status"] == "already_recorded"
    assert hh.queue == []


def test_an_unknown_entry_number_is_rejected_and_names_the_problem(client, db_session):
    _show(db_session)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()

    response = hh.judge_car("999", {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3})

    assert response["results"][0]["status"] == "error"
    assert "999" in response["results"][0]["message"]
    assert len(hh.queue) == 1  # an error is never silently dropped either — the judge can still see it queued


def test_filling_in_participant_details_on_a_blank_entry(client, db_session):
    _show(db_session)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()
    assert hh.roster["001"]["participant"] is None

    hh.judge_car(
        "001",
        {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3},
        participant="J. Smith",
        year="1972",
        vehicle_type="Truck",
    )

    assert hh.roster["001"]["participant"] == "J. Smith"
    assert hh.roster["001"]["year"] == "1972"
    assert hh.roster["001"]["vehicle_type"] == "Truck"


def test_submitting_a_manually_entered_make_and_model_records_a_candidate_sighting(client, db_session):
    _show(db_session)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()

    hh.judge_car(
        "001",
        {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3},
        make="Studebaker",
        model="Champion",
        make_manually_entered=True,
        model_manually_entered=False,
    )

    candidate = db_session.scalars(select(VehicleCandidate)).first()
    assert candidate is not None
    assert candidate.make_name == "Studebaker"
    assert candidate.model_name == "Champion"
