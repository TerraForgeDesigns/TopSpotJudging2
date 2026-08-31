"""
INT1 item 5 — photo import from a whole folder of dummy files at once
(the actual shape a real SD card scan takes — see
services/sd_card_watcher.py::scan_and_ingest_drive), not the single-file
calls tests/test_photo_ingest.py already covers exhaustively. This test
adds the folder-discovery step (find_candidate_photo_files) on top of
that already-tested per-file logic, with a deliberately mixed folder:
several real matches, one entry number matching no car (unmatched), one
duplicate of an already-imported car+type pair, and one garbage filename
that should be silently skipped, not errored.

scan_and_ingest_drive() itself opens its own SessionLocal() rather than
taking an injected session (see that module's docstring) — it can't run
against this test's in-memory db_session, so this test drives the same
two real functions it calls (find_candidate_photo_files +
ingest_photo_file) directly instead, which is what actually matters for
folder-scan behavior.
"""
from datetime import date
from pathlib import Path

import pytest
from PIL import Image

from app.models import Car, Photo, PhotoStatus, Show, TransferMethod
from app.services.photo_ingest import find_candidate_photo_files, ingest_photo_file, list_unmatched_photos


@pytest.fixture(autouse=True)
def _photos_dir(tmp_path, monkeypatch):
    photos_dir = tmp_path / "photos"
    photos_dir.mkdir()
    monkeypatch.setattr("app.services.photo_ingest.PHOTOS_DIR", photos_dir)
    return photos_dir


@pytest.fixture()
def show_with_cars(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field")
    db_session.add(show)
    db_session.commit()
    cars = [Car(show_id=show.id, entry_number=n, data_revision_at_change=1) for n in ("001", "002", "003")]
    db_session.add_all(cars)
    db_session.commit()
    return show, {c.entry_number: c for c in cars}


def _jpeg(directory: Path, filename: str, size=(20, 20), color=(120, 60, 60)) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / filename
    Image.new("RGB", size, color).save(path, "JPEG")
    return path


def test_folder_import_matches_unmatched_and_duplicate_in_one_scan(db_session, show_with_cars, tmp_path, _photos_dir):
    show, cars = show_with_cars
    card = tmp_path / "sd_card"

    # Three real cars, both photos each — the common case.
    _jpeg(card, "001_car.jpg", color=(10, 10, 10))
    _jpeg(card, "001_sheet.jpg", color=(20, 20, 20))
    _jpeg(card, "002_car.jpg", color=(30, 30, 30))
    _jpeg(card, "002_sheet.jpg", color=(40, 40, 40))
    # 003 already has a car photo on the card AND a second, different one —
    # the second must land as a duplicate, never overwrite the first.
    _jpeg(card, "003_car.jpg", color=(50, 50, 50), size=(20, 20))
    duplicate_source = _jpeg(tmp_path / "second_card", "003_car.jpg", color=(90, 90, 90), size=(40, 40))
    # An entry number with no car at all.
    _jpeg(card, "999_car.jpg", color=(70, 70, 70))
    # A file that doesn't match the naming convention at all.
    _jpeg(card, "notes.jpg", color=(80, 80, 80))

    candidates = find_candidate_photo_files(card)
    # notes.jpg deliberately excluded by the discovery step itself — see
    # find_candidate_photo_files' own contract (only names that already
    # parse as {entry}_car/_sheet.jpg count as candidates at all).
    assert all(f.name != "notes.jpg" for f in candidates)
    assert len(candidates) == 6  # 001 x2, 002 x2, 003 x1, 999 (unmatched) x1

    results = {}
    for source in candidates:
        result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)
        results.setdefault(result.status, []).append(result)

    # The second 003_car.jpg (different folder, different size) is a
    # genuine second copy for the same car+type — ingested separately.
    dup_result = ingest_photo_file(db_session, duplicate_source, show.id, TransferMethod.SD_CARD)

    assert len(results.get("matched", [])) == 5
    assert dup_result.status == "duplicate"
    assert dup_result.photo.status == PhotoStatus.DUPLICATE

    unmatched = list_unmatched_photos(db_session, show.id)
    assert len(unmatched) == 1
    assert unmatched[0].entry_number == "999"

    # The original 003_car.jpg is never overwritten by the duplicate.
    all_photos = list(db_session.query(Photo))
    car_003_photos = [p for p in all_photos if p.entry_number == "003"]
    assert len(car_003_photos) == 2
    assert sum(1 for p in car_003_photos if p.status == PhotoStatus.MATCHED) == 1
    assert sum(1 for p in car_003_photos if p.status == PhotoStatus.DUPLICATE) == 1
    for photo in car_003_photos:
        assert (_photos_dir / photo.file_path).exists()

    # The garbage-named file wrote nothing at all — silently skipped, not errored.
    assert not any(p.original_filename == "notes.jpg" for p in all_photos)
