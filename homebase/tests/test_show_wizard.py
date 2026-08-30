"""
Create Show wizard — see CONTEXT.md's "Show setup" section and
services/show_wizard.py. Three things this task specifically asked for
coverage on: wizard state surviving Back, Max Score correctness across
every range/active-category combination, and materialize() producing
exactly the right, correctly-numbered entries.
"""
import pytest
from sqlalchemy import select

from app.models import Award, Car, JudgingCategory, Show, ShowDraft
from app.services import show_wizard


# ---------------------------------------------------------------------
# Wizard state survives Back
# ---------------------------------------------------------------------

def _start(client):
    r = client.get("/shows/new")
    draft_id = int(str(r.url).rstrip("/").split("/")[-3])
    return draft_id


def test_step1_values_survive_navigating_forward_and_back(client):
    draft_id = _start(client)

    r = client.post(
        f"/shows/new/{draft_id}/step/1",
        data={"name": "Fall Cruise-In", "event_date": "2026-09-15", "location": "Downtown Lot", "notes": "Rain date TBD"},
    )
    assert r.status_code == 200
    assert "/step/2" in str(r.url)

    r = client.post(f"/shows/new/{draft_id}/step/2", data={"car_count": "200"})
    assert "/step/3" in str(r.url)

    # Navigate all the way back to step 1 without resubmitting anything —
    # the values must still be there, read straight from the draft.
    r = client.get(f"/shows/new/{draft_id}/step/1")
    assert r.status_code == 200
    assert "Fall Cruise-In" in r.text
    assert "Downtown Lot" in r.text
    assert "Rain date TBD" in r.text

    # And step 2's value survives the same way.
    r = client.get(f"/shows/new/{draft_id}/step/2")
    assert 'value="200"' in r.text


def test_step3_live_edits_survive_navigating_away_and_back(client):
    draft_id = _start(client)
    client.post(f"/shows/new/{draft_id}/step/1", data={"name": "X", "event_date": "2026-09-15"})
    client.post(f"/shows/new/{draft_id}/step/2", data={"car_count": "50"})

    # Live edits on step 3 save immediately (not via a bulk "Continue" —
    # see services/show_wizard.py's module docstring) and must still be
    # there after leaving and coming back.
    client.post(f"/shows/new/{draft_id}/step/3/category/paint/toggle")
    client.post(f"/shows/new/{draft_id}/step/3/category/engine/rename", data={"name": "Motor"})
    client.post(f"/shows/new/{draft_id}/step/3/overall-impression")

    r = client.get(f"/shows/new/{draft_id}/step/4")  # navigate forward
    assert r.status_code == 200
    r = client.get(f"/shows/new/{draft_id}/step/3")  # and back
    assert r.status_code == 200
    assert "Motor" in r.text
    # Paint's toggle persisted: it's inactive, so it won't appear in the
    # tie-break priority list (which only lists active categories).
    tie_break_section = r.text.split("which category should decide")[1]
    assert "Paint" not in tie_break_section


def test_edit_from_review_returns_to_review(client):
    draft_id = _start(client)
    client.post(f"/shows/new/{draft_id}/step/1", data={"name": "X", "event_date": "2026-09-15"})
    client.post(f"/shows/new/{draft_id}/step/2", data={"car_count": "50"})
    client.post(f"/shows/new/{draft_id}/step/3/continue")
    client.post(f"/shows/new/{draft_id}/step/4/top-awards", data={"count": "10"})
    r = client.post(f"/shows/new/{draft_id}/step/4/continue")
    assert "/step/5" in str(r.url)

    # Edit step 1 from the Review page's edit link, save, and land back
    # on Review — not march forward through steps 2/3/4 again.
    r = client.post(
        f"/shows/new/{draft_id}/step/1?from=review",
        data={"name": "Renamed Show", "event_date": "2026-09-15"},
    )
    assert "/step/5" in str(r.url)
    r = client.get(str(r.url))
    assert "Renamed Show" in r.text


# ---------------------------------------------------------------------
# Max Score correctness
# ---------------------------------------------------------------------

def _data_with(active_flags: list[bool], car_count: int) -> dict:
    data = show_wizard.new_draft_data()
    data["car_count"] = car_count
    data["categories"] = [
        {**c, "active": active} for c, active in zip(data["categories"], active_flags)
    ]
    return data


RANGE_CASES = [(1, 5), (150, 5), (151, 10), (300, 10), (301, 25), (1000, 25)]


@pytest.mark.parametrize("car_count,expected_range_max", RANGE_CASES)
@pytest.mark.parametrize("active_count", [0, 1, 2, 3, 4, 5])
def test_max_score_every_range_and_active_count_combination(car_count, expected_range_max, active_count):
    flags = [True] * active_count + [False] * (5 - active_count)
    data = _data_with(flags, car_count)
    assert show_wizard.range_max_for_draft(data) == expected_range_max
    assert show_wizard.max_score(data) == active_count * expected_range_max


def test_max_score_before_car_count_is_set_defaults_to_range_5():
    data = show_wizard.new_draft_data()  # car_count still None, all 5 active
    assert show_wizard.max_score(data) == 5 * 5


# ---------------------------------------------------------------------
# materialize() produces exactly the right, correctly-numbered entries
# ---------------------------------------------------------------------

@pytest.mark.parametrize("car_count", [1, 7, 145, 310, 1000])
def test_materialize_creates_exactly_the_right_entries(db_session, car_count):
    draft = ShowDraft(data=show_wizard.new_draft_data())
    draft.data = {**draft.data, "name": "Test Show", "event_date": "2026-09-15", "car_count": car_count, "top_awards_count": 10}
    db_session.add(draft)
    db_session.commit()

    show = show_wizard.materialize(db_session, draft)

    cars = list(db_session.scalars(select(Car).where(Car.show_id == show.id)).all())
    assert len(cars) == car_count

    entry_numbers = sorted(c.entry_number for c in cars)
    expected = sorted(f"{i:03d}" for i in range(1, car_count + 1))
    assert entry_numbers == expected
    assert len(set(entry_numbers)) == car_count  # every number unique

    # Zero-padded to at least 3 digits, and no truncation for 4-digit counts.
    assert cars[0].entry_number.startswith("0") if car_count < 100 else True
    if car_count >= 1000:
        assert any(len(n) == 4 for n in entry_numbers)


def test_materialize_deletes_the_draft_and_activates_the_show(db_session):
    draft = ShowDraft(data=show_wizard.new_draft_data())
    draft.data = {**draft.data, "name": "Test Show", "event_date": "2026-09-15", "car_count": 10, "top_awards_count": 10}
    db_session.add(draft)
    db_session.commit()
    draft_id = draft.id

    show = show_wizard.materialize(db_session, draft)

    assert db_session.get(ShowDraft, draft_id) is None
    from app.services.shows import get_active_show

    assert get_active_show(db_session).id == show.id


def test_materialize_creates_categories_and_awards_matching_the_draft(db_session):
    data = show_wizard.new_draft_data()
    data["name"] = "Test Show"
    data["event_date"] = "2026-09-15"
    data["car_count"] = 10
    data["top_awards_count"] = 10
    data["categories"] = [
        {**c, "active": c["id"] != "paint"} for c in data["categories"]
    ]
    draft = ShowDraft(data=data)
    db_session.add(draft)
    db_session.commit()

    show = show_wizard.materialize(db_session, draft)

    categories = list(db_session.scalars(select(JudgingCategory).where(JudgingCategory.show_id == show.id)))
    assert len(categories) == 5
    paint = next(c for c in categories if c.name == "Paint")
    assert paint.active is False

    awards = list(db_session.scalars(select(Award).where(Award.show_id == show.id)))
    assert len(awards) == 7
    best_paint = next(a for a in awards if a.name == "Best Paint")
    assert best_paint.judge_chosen is True
    assert best_paint.ranking_basis.value == "paint"

    assert show.configuration_revision == 1
    assert show.show_data_revision == 1
