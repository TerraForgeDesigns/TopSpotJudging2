"""
INT1 item 4 — a deliberate Top Awards boundary tie, submitted through the
real sync API (not compute_top_awards() called directly, which
tests/test_results.py already covers thoroughly at the pure-function
level, including this exact "4 cars tied for the last 2 places" case).
This is the scenario the task calls "most likely to embarrass us in
front of a crowd" — the point is proving the host is told plainly, on
the real page, rather than the system silently guessing or, worse,
finishing the show without anyone noticing the ambiguity.
"""
from tests.factories import build_show
from tests.handheld_simulator import SimulatedHandheld

CATEGORIES = ["Engine", "Exterior", "Interior", "Paint", "Wheels / Tires"]


def _score_all(handheld, entry_number, total_per_category):
    return handheld.judge_car(entry_number, {cat: total_per_category for cat in CATEGORIES})


def test_boundary_tie_is_surfaced_not_guessed(client, db_session):
    # CONTEXT.md's own example: Top 10, with 4 cars tied for the last 2
    # places. 8 cars clearly ahead (score 5/cat = 25 total), then 4 cars
    # tied at 3/cat = 15 total competing for the remaining 2 slots.
    show = build_show(db_session, car_count=12, top_awards_count=10)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()

    for i in range(8):
        _score_all(hh, f"{i + 1:03d}", 5)  # 8 clear leaders, total 25
    tied_entries = [f"{i + 1:03d}" for i in range(8, 12)]  # 009-012
    for entry in tied_entries:
        _score_all(hh, entry, 3)  # 4-way tie, total 15, for the last 2 slots

    # --- Service layer: unresolved, correct tied group ---
    from app.services.results import get_top_awards

    top_awards = get_top_awards(db_session, show)
    assert top_awards.resolved is False
    assert top_awards.slots_remaining == 2
    assert {e.car.entry_number for e in top_awards.tied_group} == set(tied_entries)

    # --- The real /results page tells the host plainly, in CONTEXT.md's exact wording ---
    page = client.get("/results")
    assert page.status_code == 200
    # Collapse whitespace — the template wraps this sentence across lines.
    body = " ".join(page.text.split())
    assert "4 cars are tied for the last 2 places in the Top 10" in body
    assert "Choose exactly 2" in body

    # --- Choosing the WRONG number of cars leaves it unresolved — no false confidence ---
    tied_car_ids = [e.car.id for e in top_awards.tied_group]
    wrong_count_response = client.post("/results/top-awards/resolve", data={"car_ids": [tied_car_ids[0]]})
    assert wrong_count_response.status_code in (200, 303)
    still_unresolved = get_top_awards(db_session, show)
    assert still_unresolved.resolved is False

    # --- Choosing exactly the right number resolves it ---
    correct_response = client.post(
        "/results/top-awards/resolve", data={"car_ids": tied_car_ids[:2]}
    )
    assert correct_response.status_code in (200, 303)
    resolved = get_top_awards(db_session, show)
    assert resolved.resolved is True
    assert len(resolved.placed) == 10
    assert {c.id for c in [e.car for e in resolved.placed]} >= set(tied_car_ids[:2])

    resolved_page = client.get("/results")
    assert "has been resolved by the host" in " ".join(resolved_page.text.split())

    # --- Finish Show succeeds regardless of Top Awards resolution state —
    # confirmed by design (CONTEXT.md only gates finishing on Show
    # Awards; Top Awards ties are surfaced on /results instead, not a
    # finish-blocking condition). Asserted explicitly here so a future
    # change to that gate breaks this test on purpose, not by accident.
    from app.services.finish_show import finish_show
    from app.services.awards_setup import list_awards
    from app.services.award_results import choose_winner, mark_not_presented

    for award in list_awards(db_session, show.id):
        if not award.active:
            continue
        if award.judge_chosen:
            mark_not_presented(db_session, show, award, True)
        else:
            choose_winner(db_session, show, award, tied_car_ids[0])

    finished = finish_show(db_session, show)
    assert finished is True
