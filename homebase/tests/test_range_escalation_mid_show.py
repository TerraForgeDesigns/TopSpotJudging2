"""
INT1 item 3 — a show that crosses a score range threshold mid-judging,
driven through the real sync API end to end. Complements (does not
duplicate) tests/test_range_escalation.py::test_escalation_full_scenario,
which already unit-tests check_and_escalate() directly with hand-inserted
rows. Here: 140 cars judged at 1-5 through real SimulatedHandhelds, 40
more cars added via the real (now-fixed — see DECISIONS.md) add_cars()
service, and — the one case the existing unit test can't reach — a
handheld that judged a car BEFORE the escalation but doesn't sync it
until AFTER, converting on arrival exactly like
tests/test_sync_idempotency.py::test_stale_score_range_max_is_converted_on_receipt,
but as the natural consequence of this test's own mid-show narrative.
"""
from sqlalchemy import select

from app.models import JudgingScore, JudgingSubmission
from app.services.cars import add_cars
from app.services.scoring import overall_rankings
from tests.factories import build_show
from tests.handheld_simulator import SimulatedHandheld

CATEGORIES = ["Engine", "Exterior", "Interior", "Paint", "Wheels / Tires"]

# CONTEXT.md's verified 1-5 -> 1-10 conversion table.
CONVERSION_1_5_TO_1_10 = {1: 2, 2: 4, 3: 6, 4: 8, 5: 10}


def test_140_cars_at_1_5_then_40_more_cars_escalates_and_converts_correctly(client, db_session):
    show = build_show(db_session, car_count=140)
    assert show.score_range_max == 5  # 140 cars: still in the 1-150 tier

    hh1 = SimulatedHandheld(client, "hh-1")
    hh2 = SimulatedHandheld(client, "hh-2")
    hh1.check_in()
    hh2.check_in()

    # 140 cars judged at 1-5, varying points so relative order is meaningful.
    original_points_by_entry: dict[str, int] = {}
    for i in range(140):
        entry_number = f"{i + 1:03d}"
        points = (i % 5) + 1  # cycles 1..5
        hh = hh1 if i % 2 == 0 else hh2
        response = hh.judge_car(entry_number, {cat: points for cat in CATEGORIES})
        assert response["results"][0]["status"] == "accepted"
        original_points_by_entry[entry_number] = points

    # A third handheld drops out of range now, before the escalation, and
    # stays out of range through it — it will judge one of the 40 NEW
    # cars (added just below) while still out of range, then come back
    # into range only after the escalation has already happened server
    # side. That's the genuine "queued but unsent across an escalation"
    # case — set up here, exercised further down.
    hh3 = SimulatedHandheld(client, "hh-3")
    hh3.check_in()
    hh3.go_out_of_range()

    # Home Base's side: 40 more cars added mid-show (140 + 40 = 180,
    # crossing the 150 threshold) via the REAL add_cars() service — this
    # is the exact path DECISIONS.md's SIM1 entry fixed a real flush bug
    # in; using it here (rather than a hand-rolled insert loop) means this
    # test would have caught that bug itself.
    add_cars(db_session, show, 40)
    db_session.refresh(show)
    assert show.score_range_max == 10  # 180 cars crosses 150 -> escalates

    # --- Every already-stored score converted correctly, originals preserved ---
    scores = list(
        db_session.scalars(select(JudgingScore).join(JudgingSubmission).where(JudgingSubmission.show_id == show.id))
    )
    assert len(scores) == 140 * len(CATEGORIES)
    for score in scores:
        assert score.original_range_max == 5
        assert score.adjusted_points == CONVERSION_1_5_TO_1_10[score.original_points]

    # --- Relative ranking among the 140 already-judged cars is unchanged ---
    ranked = overall_rankings(db_session, show.id)
    adjusted_by_entry = {}
    for entry in ranked:
        submission = entry.car.submissions[0]
        adjusted_by_entry[entry.car.entry_number] = sum(s.adjusted_points for s in submission.scores)
    for entry_a, points_a in original_points_by_entry.items():
        for entry_b, points_b in original_points_by_entry.items():
            if points_a > points_b:
                assert adjusted_by_entry[entry_a] > adjusted_by_entry[entry_b]
            elif points_a == points_b:
                assert adjusted_by_entry[entry_a] == adjusted_by_entry[entry_b]

    # --- The queued-but-unsent car: judged while the show was still at
    # 1-5, synced only after the escalation, converts on arrival ---
    hh3.come_back_into_range()
    # hh3's own configuration is stale (it last saw the show at 1-5) — it
    # judges a new car using the range it still believes is current.
    new_entry = "141"  # first of the 40 newly added cars
    stale_range = hh3.configuration["score_range_max"]  # 5, from before escalation
    assert stale_range == 5
    hh3.queue.append(
        {
            "entry_number": new_entry,
            "judge_name": None,
            "closed_at_uptime_ms": 999999,
            "participant": None,
            "year": None,
            "make": None,
            "model": None,
            "vehicle_type": None,
            "make_manually_entered": False,
            "model_manually_entered": False,
            "score_range_max": stale_range,
            "scores": [{"category_id": hh3.category_id(cat), "points": 3} for cat in CATEGORIES],
            "overall_impression": None,
            "nominations": [],
            "vehicle_photo_path": "/sdcard/topspot/photos/vehicle/vehicle_141_00999999.jpg",
            "judge_sheet_photo_path": "/sdcard/topspot/photos/judge_sheets/judge_sheet_141_00999999.jpg",
        }
    )
    response = hh3.check_in()
    assert response["results"][0]["status"] == "accepted"

    new_submission = db_session.scalars(
        select(JudgingSubmission).where(JudgingSubmission.entry_number == new_entry)
    ).first()
    assert new_submission is not None
    for score in new_submission.scores:
        assert score.original_points == 3
        assert score.original_range_max == 5  # audit trail: sent at the OLD range
        assert score.adjusted_points == CONVERSION_1_5_TO_1_10[3]  # 6 — converted on receipt

    # --- A handheld that was still in range and had the OLD config gets
    # the new configuration (score_range_max: 10) on its next check-in —
    # the exact server-side condition that fires the Scoring Updated
    # notice on a real handheld (see DECISIONS.md's SIM1 entry). ---
    assert hh1.configuration["score_range_max"] == 5  # hasn't refreshed yet
    hh1_response = hh1.periodic_tick()
    assert hh1_response["configuration"] is not None
    assert hh1_response["configuration"]["score_range_max"] == 10
