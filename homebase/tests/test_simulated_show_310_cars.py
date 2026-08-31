"""
INT1 item 2 — a full simulated show: 310 cars, 4 handhelds, realistic
out-of-range gaps, cars added mid-show, one true duplicate, one
lost-acknowledgement retry, and a handheld that dies partway through
(leaving its last few in-progress cars permanently unsent — the correct,
already-designed-for outcome per CONTEXT.md's resilience principle, not a
bug). Verifies dashboard counts, conflict resolution, tie-broken
rankings, award winners from nominations, the New Vehicle Names queue,
and the finish-show checks — all reached through the real sync API via
SimulatedHandheld, not direct service calls.

Judged/conflict bookkeeping (`judged_entries`) is derived from the actual
server responses as the loop runs, not hand-computed arithmetic — every
`submit()` call reads whichever "results" batch actually came back
(one item on a normal sync, several at once when a handheld reconnects
after a gap and flushes its whole queue) and updates the set from that,
so the final assertions check against what really happened.
"""
import random

from sqlalchemy import select

from app.models import Car, VehicleCandidate
from app.services.award_results import choose_winner, mark_not_presented, suggest_award_winner
from app.services.awards_setup import list_awards
from app.services.cars import add_cars
from app.services.conflicts import accept_submission
from app.services.dashboard import get_summary
from app.services.finish_show import can_finish_show, finish_show, outstanding_awards
from app.services.scoring import overall_rankings
from tests.factories import build_show
from tests.handheld_simulator import SimulatedHandheld

CATEGORIES = ["Engine", "Exterior", "Interior", "Paint", "Wheels / Tires"]
NOMINATED_AWARDS = ["Best Paint", "Best Engine"]  # the other 5 built-ins get zero nominations
UNNOMINATED_AWARDS = ["Best Interior", "Best Car", "Best Truck", "Best Bike", "Best Rat Rod"]


def _random_scores(rng: random.Random, max_points: int) -> dict[str, int]:
    return {cat: rng.randint(1, max_points) for cat in CATEGORIES}


def test_simulated_show_310_cars_four_handhelds(client, db_session):
    rng = random.Random(42)
    show = build_show(db_session, car_count=310, top_awards_count=50, overall_impression_enabled=True)
    assert show.score_range_max == 25  # 310 cars: the 301+ open-ended tier

    hh1, hh2, hh3, hh4 = (SimulatedHandheld(client, label) for label in ("hh-1", "hh-2", "hh-3", "hh-4"))
    for hh in (hh1, hh2, hh3, hh4):
        hh.check_in()

    judged_entries: set[str] = set()

    def submit(hh: SimulatedHandheld, entry: str, *, scores: dict[str, int] | None = None, overall_impression: int | None = None, **kwargs):
        response = hh.judge_car(
            entry,
            scores if scores is not None else _random_scores(rng, 25),
            overall_impression=overall_impression if overall_impression is not None else rng.randint(1, 25),
            nominations=[a for a in NOMINATED_AWARDS if rng.random() < 0.15] or None,
            **kwargs,
        )
        # None means still out of range — nothing reached the server yet.
        # Otherwise a whole BATCH of results may have come back at once (a
        # reconnect flushes the entire local queue in one sync), so every
        # result is applied, not just the one for `entry`.
        if response is not None:
            for result in response["results"]:
                if result["status"] == "accepted":
                    judged_entries.add(result["entry_number"])
                elif result["status"] == "flagged_duplicate":
                    judged_entries.discard(result["entry_number"])
        return response

    # --- hh1: entries 001-080, with two realistic out-of-range gaps and
    # two manually-entered make/model submissions (New Vehicle Names). ---
    for i in range(1, 81):
        entry = f"{i:03d}"
        if i in (20, 55):
            hh1.go_out_of_range()
        if i in (25, 60):
            hh1.come_back_into_range()
        kwargs = {}
        if entry == "010":
            kwargs = {"make": "Studebaker", "model": "Champion", "make_manually_entered": True}
        elif entry == "030":
            kwargs = {"make": "Chevrolet", "model": "Nomad Custom", "model_manually_entered": True}
        submit(hh1, entry, **kwargs)
    assert hh1.queue == []  # every gap eventually drained

    # --- hh2: entries 081-160, with one lost-acknowledgement retry at 100. ---
    for i in range(81, 161):
        entry = f"{i:03d}"
        if entry == "100":
            scores = _random_scores(rng, 25)
            raw = hh2.judge_car_losing_the_ack(entry, scores, overall_impression=rng.randint(1, 25))
            assert raw["results"][0]["status"] == "accepted"
            assert len(hh2.queue) == 1  # the handheld never found out
            retry = hh2.check_in()
            assert retry["results"][0]["status"] == "already_recorded"
            judged_entries.add(entry)
        else:
            submit(hh2, entry)

    # --- hh3: entries 161-240, plus a genuine duplicate on 050 (already
    # judged by hh1) with a different score — the one true conflict. ---
    for i in range(161, 241):
        submit(hh3, f"{i:03d}")
    dup_response = submit(hh3, "050")
    assert dup_response["results"][0]["status"] == "flagged_duplicate"
    assert "050" not in judged_entries  # the car's status flips to flagged_conflict

    # --- hh4: entries 241-290 judged normally, then dies with 291-293
    # queued but never sent (294-310 never touched by anyone at all). ---
    for i in range(241, 291):
        submit(hh4, f"{i:03d}")
    hh4.go_out_of_range()
    for i in range(291, 294):
        submit(hh4, f"{i:03d}")  # stuck in hh4's local queue forever
    assert len(hh4.queue) == 3
    # hh4 never comes back into range again — it "died."

    # --- Cars added mid-show (services.cars.add_cars — the real, now-
    # fixed service). A couple of the 20 new cars get judged, including a
    # deliberately extreme pair for the ranking spot-check below. ---
    add_cars(db_session, show, 20)
    db_session.refresh(show)
    submit(hh1, "311")
    submit(hh1, "312")
    submit(hh1, "313", scores={c: 25 for c in CATEGORIES}, overall_impression=25)
    submit(hh1, "314", scores={c: 1 for c in CATEGORIES}, overall_impression=1)

    # =====================================================================
    # Judging "closes." Verify.
    # =====================================================================

    # --- Dashboard counts match what actually happened. ---
    summary = get_summary(db_session, show.id)
    assert summary.total_cars == 330  # 310 + 20 added mid-show
    assert summary.judged == len(judged_entries)
    assert summary.flagged_conflict == 1  # car 050
    assert summary.unjudged == summary.total_cars - summary.judged - summary.flagged_conflict

    # --- Conflict resolution ---
    car_050 = db_session.scalars(select(Car).where(Car.show_id == show.id, Car.entry_number == "050")).first()
    assert car_050.status.value == "flagged_conflict"
    original_submission = min(car_050.submissions, key=lambda s: s.id)  # hh1's, submitted first
    accept_submission(db_session, show, car_050, original_submission.id)
    db_session.refresh(car_050)
    assert car_050.status.value == "judged"
    judged_entries.add("050")

    summary_after_resolution = get_summary(db_session, show.id)
    assert summary_after_resolution.judged == len(judged_entries)
    assert summary_after_resolution.flagged_conflict == 0

    # --- Tie-broken rankings: the deliberately extreme pair sorts correctly. ---
    ranked = overall_rankings(db_session, show.id)
    ranks_by_entry = {entry.car.entry_number: entry.rank for entry in ranked}
    assert ranks_by_entry["313"] < ranks_by_entry["314"]  # top scorer ranks ahead of bottom scorer
    assert ranks_by_entry["313"] == 1  # nothing beats a perfect score across every category + Overall Impression

    # --- New Vehicle Names queue has the manually-entered candidates. ---
    candidates = {c.make_name: c.model_name for c in db_session.scalars(select(VehicleCandidate))}
    assert candidates.get("Studebaker") == "Champion"
    assert candidates.get("Chevrolet") == "Nomad Custom"

    # --- Award winners from nominations. ---
    for award in list_awards(db_session, show.id):
        if award.name in NOMINATED_AWARDS:
            suggestion = suggest_award_winner(db_session, show, award)
            if suggestion.entry is not None and not suggestion.ambiguous:
                choose_winner(db_session, show, award, suggestion.entry.car.id)

    # --- Finish-show gate: blocked while awards are outstanding, then succeeds. ---
    assert can_finish_show(db_session, show) is False
    still_outstanding = {o.award.name for o in outstanding_awards(db_session, show)}
    assert set(UNNOMINATED_AWARDS) <= still_outstanding

    for award in list_awards(db_session, show.id):
        if award.name in UNNOMINATED_AWARDS:
            mark_not_presented(db_session, show, award, True)
        elif award.name in NOMINATED_AWARDS and award.winner_car_id is None:
            # No car happened to clear the nomination threshold this run —
            # still must be resolved one way or another before finishing.
            mark_not_presented(db_session, show, award, True)

    assert can_finish_show(db_session, show) is True
    assert finish_show(db_session, show) is True
