from datetime import date

import pytest
from sqlalchemy import select

from app.models import Award, Car, Handheld, JudgingCategory, JudgingSubmission, Show, SubmissionSource
from app.models.enums import CarStatus
from app.services.dashboard import get_summary
from app.services.shows import set_active_show


def _make_show(db_session, car_count=2, score_range_max=5):
    show = Show(name="Lake Side Live 2026", event_date=date(2026, 9, 19), score_range_max=score_range_max)
    db_session.add(show)
    db_session.commit()
    set_active_show(db_session, show.id)

    cats = [
        JudgingCategory(show_id=show.id, name="Engine", sort_order=0, active=True),
        JudgingCategory(show_id=show.id, name="Exterior", sort_order=1, active=True),
        JudgingCategory(show_id=show.id, name="Interior", sort_order=2, active=True),
        JudgingCategory(show_id=show.id, name="Paint", sort_order=3, active=True),
        JudgingCategory(show_id=show.id, name="Wheels/Tires", sort_order=4, active=False),
    ]
    db_session.add_all(cats)
    awards = [
        Award(show_id=show.id, name="Best Paint", sort_order=0, active=True, judge_chosen=True),
        Award(show_id=show.id, name="Best Interior", sort_order=1, active=False, judge_chosen=True),
    ]
    db_session.add_all(awards)
    cars = [
        Car(show_id=show.id, entry_number=f"{index:03d}", data_revision_at_change=show.show_data_revision)
        for index in range(1, car_count + 1)
    ]
    db_session.add_all(cars)
    db_session.commit()
    return show, cats, awards, cars


def _sync(
    client,
    handheld_id="TS-HH-A001",
    config_revision=0,
    data_revision=0,
    submissions=None,
    protocol_version=1,
    known_car_count=0,
):
    response = client.post(
        "/api/v1/sync",
        json={
            "handheld_id": handheld_id,
            "config_revision": config_revision,
            "data_revision": data_revision,
            "known_car_count": known_car_count,
            "firmware_version": "p4-local-0.1",
            "protocol_version": protocol_version,
            "rssi_dbm": -44,
            "submissions": submissions or [],
        },
    )
    return response


def _handshake(client, handheld_id="TS-HH-A001", protocol_version=1):
    return client.post(
        "/api/v1/handshake",
        json={
            "handheld_id": handheld_id,
            "firmware_version": "p4-local-0.1",
            "protocol_version": protocol_version,
            "rssi_dbm": -44,
        },
    )


def _submission(entry_number, categories, local_record_id="local-00000001", closed_at=1):
    return {
        "local_record_id": local_record_id,
        "entry_number": entry_number,
        "closed_at_uptime_ms": closed_at,
        "participant": "Judge Test",
        "year": "1969",
        "make": "Chevrolet",
        "model": "Camaro",
        "vehicle_type": "Car",
        "score_range_max": 5,
        "scores": [{"category_id": c.id, "points": 4} for c in categories if c.active],
        "nominations": [],
        "vehicle_photo_path": "/sdcard/topspot/photos/vehicle/test.jpg",
        "judge_sheet_photo_path": "/sdcard/topspot/photos/judge_sheets/test.jpg",
    }


def test_handshake_with_active_show_returns_ok(client, db_session):
    show, cats, awards, cars = _make_show(db_session)

    response = _handshake(client)

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["status"] == "ok"
    assert body["handheld_id"] == "TS-HH-A001"
    assert body["show_name"] == show.name


def test_handshake_without_active_show_returns_ok_with_no_show(client):
    response = _handshake(client)

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["status"] == "ok"
    assert body["handheld_id"] == "TS-HH-A001"
    assert body["show_name"] is None


def test_active_show_config_excludes_disabled_categories_and_awards(client, db_session):
    show, cats, awards, cars = _make_show(db_session)

    response = _sync(client)

    assert response.status_code == 200, response.text
    config = response.json()["configuration"]
    assert config["show_id"] == show.id
    assert config["show_name"] == "Lake Side Live 2026"
    assert config["show_date"] == "2026-09-19"
    assert config["config_revision"] == show.configuration_revision
    assert [c["name"] for c in config["categories"]] == ["Engine", "Exterior", "Interior", "Paint"]
    assert all(c["id"] for c in config["categories"])
    assert "Wheels/Tires" not in [c["name"] for c in config["categories"]]
    assert [a["name"] for a in config["judge_chosen_awards"]] == ["Best Paint"]


def test_config_only_sync_response_shape_matches_p4_parser(client, db_session):
    show, cats, awards, cars = _make_show(db_session)

    response = _sync(
        client,
        config_revision=0,
        data_revision=show.show_data_revision,
        submissions=[],
        known_car_count=len(cars),
    )

    assert response.status_code == 200, response.text
    body = response.json()
    assert set(body) == {
        "server_time",
        "config_revision",
        "data_revision",
        "sync_mode",
        "configuration",
        "cars",
        "vehicle_additions",
        "results",
        "summary",
    }
    assert isinstance(body["config_revision"], int)
    assert isinstance(body["data_revision"], int)
    assert body["cars"] == []
    assert body["vehicle_additions"] == []
    assert body["results"] == []

    config = body["configuration"]
    assert set(config) == {
        "show_id",
        "show_name",
        "show_date",
        "config_revision",
        "active",
        "score_range_max",
        "max_score",
        "overall_impression_enabled",
        "categories",
        "judge_chosen_awards",
    }
    assert config["show_id"] == show.id
    assert isinstance(config["show_id"], int)
    assert config["show_name"] == "Lake Side Live 2026"
    assert config["show_date"] == "2026-09-19"
    assert isinstance(config["config_revision"], int)
    assert isinstance(config["score_range_max"], int)
    assert isinstance(config["categories"], list)
    assert isinstance(config["judge_chosen_awards"], list)
    assert [set(category) for category in config["categories"]] == [
        {"id", "name", "sort_order"} for _ in config["categories"]
    ]
    assert [category["name"] for category in config["categories"]] == ["Engine", "Exterior", "Interior", "Paint"]
    assert "Wheels/Tires" not in [category["name"] for category in config["categories"]]
    assert [set(award) for award in config["judge_chosen_awards"]] == [
        {"id", "name"} for _ in config["judge_chosen_awards"]
    ]
    assert [award["name"] for award in config["judge_chosen_awards"]] == ["Best Paint"]


def test_large_production_sync_response_preserves_active_configuration_contract(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=300)

    response = _sync(client, config_revision=0, data_revision=0, submissions=[])

    assert response.status_code == 200, response.text
    assert len(response.text) > 8192
    body = response.json()
    config = body["configuration"]
    assert config["show_id"] == show.id
    assert isinstance(config["show_id"], int)
    assert config["show_name"] == "Lake Side Live 2026"
    assert config["show_date"] == "2026-09-19"
    assert config["config_revision"] == show.configuration_revision
    assert config["active"] is True
    assert config["score_range_max"] == 5
    assert config["max_score"] == 20
    assert config["overall_impression_enabled"] is False
    assert [category["name"] for category in config["categories"]] == ["Engine", "Exterior", "Interior", "Paint"]
    assert "Wheels/Tires" not in [category["name"] for category in config["categories"]]
    assert [award["name"] for award in config["judge_chosen_awards"]] == ["Best Paint"]
    assert len(body["cars"]) == 300
    assert {"id", "entry_number", "status", "data_revision", "judged_source", "judged_at"}.issubset(
        set(body["cars"][0])
    )


def test_sync_without_active_show_is_rejected_cleanly(client):
    response = _sync(client, config_revision=0, data_revision=0, submissions=[])

    assert response.status_code == 409
    assert response.json()["detail"] == "No active show configured on home base."


def test_multiple_handhelds_sync_independently(client, db_session):
    _make_show(db_session)

    for index in range(10):
        response = _sync(client, handheld_id=f"TS-HH-{index:04X}")
        assert response.status_code == 200, response.text

    handhelds = list(db_session.scalars(select(Handheld).order_by(Handheld.label)))
    assert len(handhelds) == 10
    assert {h.label for h in handhelds} == {f"TS-HH-{index:04X}" for index in range(10)}
    assert all(h.last_sync_at is not None for h in handhelds)


def test_pending_record_upload_marks_car_judged_and_dashboard_updates(client, db_session):
    show, cats, awards, cars = _make_show(db_session)

    before = get_summary(db_session, show.id)
    assert before.judged == 0
    assert before.unjudged == 2

    response = _sync(client, submissions=[_submission("002", cats)])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "accepted"

    after = get_summary(db_session, show.id)
    assert after.judged == 1
    assert after.unjudged == 1
    car = db_session.scalars(select(Car).where(Car.entry_number == "002")).one()
    assert car.status == CarStatus.JUDGED
    assert db_session.scalars(select(JudgingSubmission)).first() is not None


def test_handheld_submission_requires_photos(client, db_session):
    show, cats, awards, cars = _make_show(db_session)
    payload = _submission("001", cats)
    payload["vehicle_photo_path"] = ""

    response = _sync(client, submissions=[payload])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "error"
    assert "require vehicle and judge sheet photos" in body["results"][0]["message"]
    assert list(db_session.scalars(select(JudgingSubmission))) == []


def test_handheld_submission_cannot_exceed_active_show_score_range(client, db_session):
    show, cats, awards, cars = _make_show(db_session, score_range_max=5)
    payload = _submission("001", cats)
    payload["score_range_max"] = 10
    for score in payload["scores"]:
        score["points"] = 6

    response = _sync(client, submissions=[payload])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "error"
    assert "outside the allowed range (1-5)" in body["results"][0]["message"]
    assert list(db_session.scalars(select(JudgingSubmission))) == []


def _manual_form(categories, awards, score=4, replace=False):
    data = {
        "participant": "Manual Judge",
        "year": "1970",
        "make": "Ford",
        "model": "Mustang",
        "operator_label": "Admin",
    }
    if replace:
        data["replace_existing"] = "1"
    for category in categories:
        if category.active:
            data[f"score_{category.id}"] = str(score)
    active_awards = [award for award in awards if award.active and award.judge_chosen]
    if active_awards:
        data["nominations"] = [str(active_awards[0].id)]
    return data


def test_manual_judge_unjudged_car_without_photos(client, db_session):
    show, cats, awards, cars = _make_show(db_session)
    before_revision = show.show_data_revision

    response = client.post(f"/cars/{cars[0].id}/judge", data=_manual_form(cats, awards), follow_redirects=False)

    assert response.status_code == 303
    db_session.refresh(show)
    db_session.refresh(cars[0])
    assert show.show_data_revision == before_revision + 1
    assert cars[0].status == CarStatus.JUDGED
    submission = db_session.scalars(select(JudgingSubmission)).one()
    assert submission.source == SubmissionSource.HOMEBASE_MANUAL
    assert submission.handheld_id is None
    assert submission.vehicle_photo_required is False
    assert submission.judge_sheet_photo_required is False
    assert submission.vehicle_photo_path is None
    assert submission.judge_sheet_photo_path is None
    assert len(submission.scores) == 4
    assert all(score.category.active for score in submission.scores)
    assert [nom.award.name for nom in submission.nominations] == ["Best Paint"]


def test_manual_judge_uses_active_show_1_to_5_range_for_page_and_validation(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=3, score_range_max=5)

    page = client.get(f"/cars/{cars[0].id}/judge")
    assert page.status_code == 200
    assert "Score each active category from 1 to 5" in page.text
    assert ">5</option>" in page.text
    assert ">6</option>" not in page.text

    accepted = client.post(f"/cars/{cars[0].id}/judge", data=_manual_form(cats, awards, score=5), follow_redirects=False)
    rejected = client.post(f"/cars/{cars[1].id}/judge", data=_manual_form(cats, awards, score=6), follow_redirects=False)

    assert accepted.status_code == 303
    assert rejected.status_code == 400
    assert "outside the allowed range (1-5)" in rejected.text
    db_session.refresh(show)
    submissions = list(db_session.scalars(select(JudgingSubmission)))
    assert len(submissions) == 1
    assert {score.original_points for score in submissions[0].scores} == {5}


def test_manual_judge_uses_active_show_1_to_10_range_for_page_and_validation(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=3, score_range_max=10)

    page = client.get(f"/cars/{cars[0].id}/judge")
    assert page.status_code == 200
    assert "Score each active category from 1 to 10" in page.text
    assert ">10</option>" in page.text
    assert ">11</option>" not in page.text

    accepted = client.post(f"/cars/{cars[0].id}/judge", data=_manual_form(cats, awards, score=10), follow_redirects=False)
    rejected = client.post(f"/cars/{cars[1].id}/judge", data=_manual_form(cats, awards, score=11), follow_redirects=False)

    assert accepted.status_code == 303
    assert rejected.status_code == 400
    assert "outside the allowed range (1-10)" in rejected.text
    db_session.refresh(show)
    submissions = list(db_session.scalars(select(JudgingSubmission)))
    assert len(submissions) == 1
    assert {score.original_points for score in submissions[0].scores} == {10}


def test_manual_judge_already_judged_requires_explicit_replace(client, db_session):
    show, cats, awards, cars = _make_show(db_session)
    first = client.post(f"/cars/{cars[0].id}/judge", data=_manual_form(cats, awards, score=4), follow_redirects=False)
    second = client.post(f"/cars/{cars[0].id}/judge", data=_manual_form(cats, awards, score=5), follow_redirects=False)

    assert first.status_code == 303
    assert second.status_code == 400
    assert "Already Judged" in second.text
    assert len(list(db_session.scalars(select(JudgingSubmission)))) == 1

    replace = client.post(f"/cars/{cars[0].id}/judge", data=_manual_form(cats, awards, score=5, replace=True), follow_redirects=False)

    assert replace.status_code == 303
    submissions = list(db_session.scalars(select(JudgingSubmission).order_by(JudgingSubmission.id)))
    assert len(submissions) == 2
    assert submissions[0].status.value == "rejected"
    assert submissions[1].source == SubmissionSource.HOMEBASE_MANUAL


def test_sync_reflects_manually_judged_car_status_without_photo_paths(client, db_session):
    show, cats, awards, cars = _make_show(db_session)
    response = client.post(f"/cars/{cars[1].id}/judge", data=_manual_form(cats, awards), follow_redirects=False)
    assert response.status_code == 303
    db_session.refresh(show)

    sync_response = _sync(client, config_revision=show.configuration_revision, data_revision=0)

    assert sync_response.status_code == 200, sync_response.text
    cars_by_entry = {car["entry_number"]: car for car in sync_response.json()["cars"]}
    car = cars_by_entry["002"]
    assert car["status"] == "judged"
    assert car["judged_source"] == "homebase_manual"
    assert car["judged_at"] is not None
    assert "vehicle_photo_path" not in car
    assert "judge_sheet_photo_path" not in car


def test_numeric_handheld_entry_resolves_to_generated_roster_number(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=12)

    response = _sync(client, submissions=[_submission("12", cats)])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "accepted"
    assert body["results"][0]["entry_number"] == "012"

    car = db_session.scalars(select(Car).where(Car.show_id == show.id, Car.entry_number == "012")).one()
    assert car.status == CarStatus.JUDGED
    submission = db_session.scalars(select(JudgingSubmission)).one()
    assert submission.entry_number == "012"
    assert submission.show_id == show.id


@pytest.mark.parametrize(
    ("entered", "expected"),
    [
        ("20", "020"),
        ("020", "020"),
        ("1", "001"),
        ("12", "012"),
        ("300", "300"),
    ],
)
def test_numeric_entry_resolution_uses_active_show_roster(client, db_session, entered, expected):
    show, cats, awards, cars = _make_show(db_session, car_count=300)

    response = _sync(client, submissions=[_submission(entered, cats)])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "accepted"
    assert body["results"][0]["entry_number"] == expected

    submission = db_session.scalars(select(JudgingSubmission)).one()
    assert submission.entry_number == expected
    assert submission.show_id == show.id
    assert submission.car.entry_number == expected


def test_nonexistent_numeric_entry_is_rejected(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=12)

    response = _sync(client, submissions=[_submission("20", cats)])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "error"
    assert body["results"][0]["message"] == "Entry number '20' not found."
    assert list(db_session.scalars(select(JudgingSubmission))) == []


def test_numeric_entry_resolution_is_scoped_to_active_show(client, db_session):
    inactive = Show(name="Inactive", event_date=date(2026, 9, 18), score_range_max=5)
    db_session.add(inactive)
    db_session.commit()
    db_session.add(Car(show_id=inactive.id, entry_number="020", data_revision_at_change=inactive.show_data_revision))
    active, cats, awards, cars = _make_show(db_session, car_count=12)

    response = _sync(client, submissions=[_submission("20", cats)])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "error"
    assert body["results"][0]["message"] == "Entry number '20' not found."
    assert list(db_session.scalars(select(JudgingSubmission))) == []


def test_nonnumeric_entry_exact_match_is_preserved(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=0)
    car = Car(show_id=show.id, entry_number="A-20", data_revision_at_change=show.show_data_revision)
    db_session.add(car)
    db_session.commit()

    response = _sync(client, submissions=[_submission("A-20", cats)])

    assert response.status_code == 200, response.text
    body = response.json()
    assert body["results"][0]["status"] == "accepted"
    assert body["results"][0]["entry_number"] == "A-20"
    submission = db_session.scalars(select(JudgingSubmission)).one()
    assert submission.entry_number == "A-20"
    assert submission.car_id == car.id


def test_batch_rejects_bad_entry_but_accepts_following_valid_entry(client, db_session):
    show, cats, awards, cars = _make_show(db_session, car_count=12)

    response = _sync(
        client,
        submissions=[
            _submission("999", cats, local_record_id="local-00000006", closed_at=6),
            _submission("12", cats, local_record_id="local-00000007", closed_at=7),
        ],
    )

    assert response.status_code == 200, response.text
    body = response.json()
    assert [r["status"] for r in body["results"]] == ["error", "accepted"]
    assert body["results"][0]["message"] == "Entry number '999' not found."
    assert body["results"][1]["entry_number"] == "012"

    after = get_summary(db_session, show.id)
    assert after.judged == 1
    assert after.unjudged == 11
    assert db_session.scalars(select(Car).where(Car.show_id == show.id, Car.entry_number == "012")).one().status == CarStatus.JUDGED
    assert len(list(db_session.scalars(select(JudgingSubmission)))) == 1


def test_duplicate_retry_same_idempotency_key_returns_success_without_duplicate(client, db_session):
    show, cats, awards, cars = _make_show(db_session)
    payload = _submission("001", cats, local_record_id="local-00000012", closed_at=12)

    first = _sync(client, handheld_id="TS-HH-2A08", submissions=[payload])
    second = _sync(client, handheld_id="TS-HH-2A08", submissions=[payload])

    assert first.status_code == 200, first.text
    assert second.status_code == 200, second.text
    assert first.json()["results"][0]["status"] == "accepted"
    assert second.json()["results"][0]["status"] == "already_recorded"
    assert len(list(db_session.scalars(select(JudgingSubmission)))) == 1


def test_duplicate_retry_with_numeric_entry_alias_is_idempotent(client, db_session):
    show, cats, awards, cars = _make_show(db_session)
    payload = _submission("1", cats, local_record_id="local-00000012", closed_at=12)

    first = _sync(client, handheld_id="TS-HH-2A08", submissions=[payload])
    second = _sync(client, handheld_id="TS-HH-2A08", submissions=[payload])

    assert first.status_code == 200, first.text
    assert second.status_code == 200, second.text
    assert first.json()["results"][0]["status"] == "accepted"
    assert first.json()["results"][0]["entry_number"] == "001"
    assert second.json()["results"][0]["status"] == "already_recorded"
    assert second.json()["results"][0]["entry_number"] == "001"
    assert len(list(db_session.scalars(select(JudgingSubmission)))) == 1


def test_protocol_version_mismatch_is_rejected(client, db_session):
    _make_show(db_session)

    response = _sync(client, protocol_version=999)

    assert response.status_code == 426
