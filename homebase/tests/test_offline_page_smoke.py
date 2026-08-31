"""
INT1 item 6 — offline verification, the automated half. Static analysis
already confirmed zero external references anywhere in
homebase/app/templates or homebase/app/static (see DECISIONS.md's INT1
entry) and requirements.txt is fully pinned. This test adds one more
layer: every GET page route actually renders (200, no exception) against
a real populated show, through the SAME TestClient this whole suite
already treats as "the real API" — and TestClient makes no real network
call of any kind (SQLite is the only I/O), so a passing run here is
concrete evidence every screen works with zero connectivity, not just an
absence-of-CDN-link argument.

What this does NOT replace: the literal "open DevTools, watch the
Network tab, physically unplug the internet" check — that needs a real
browser and a real network to switch off, neither of which exists in
this environment. That step lives in docs/show-day-runbook.md instead.
"""
import pytest

from tests.factories import build_show
from tests.handheld_simulator import SimulatedHandheld

PAGE_ROUTES = [
    "/",
    "/shows",
    "/dashboard/live",
    "/cars",
    "/handhelds",
    "/awards",
    "/awards/present",
    "/photos",
    "/photos/unmatched",
    "/results",
    "/results/print",
    "/simulator",
    "/styleguide",
]


@pytest.fixture()
def populated_show(db_session, client):
    show = build_show(db_session, car_count=5, top_awards_count=10, overall_impression_enabled=True)
    hh = SimulatedHandheld(client, "hh-1")
    hh.check_in()
    hh.judge_car(
        "001",
        {"Engine": 4, "Exterior": 3, "Interior": 5, "Paint": 4, "Wheels / Tires": 3},
        overall_impression=4,
        nominations=["Best Paint"],
    )
    return show


@pytest.mark.parametrize("route", PAGE_ROUTES)
def test_page_renders_offline_with_no_external_calls(client, populated_show, route):
    response = client.get(route)
    assert response.status_code == 200, f"{route} -> {response.status_code}: {response.text[:300]}"


def test_results_export_csv_renders_offline(client, populated_show):
    response = client.get("/results/export.csv")
    assert response.status_code == 200
    assert "text/csv" in response.headers["content-type"]
