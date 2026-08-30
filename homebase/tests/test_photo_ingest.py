"""
The shared photo ingest path — see CONTEXT.md and PROTOCOL.md's photo
naming convention. Used identically by the SD card watcher and the Wi-Fi
upload fallback (see services/photo_ingest.py's module docstring) —
these tests exercise ingest_photo_file() directly, which is exactly what
both callers do, so there's no need to duplicate coverage per transport.
"""
from datetime import date
from pathlib import Path

import pytest
from PIL import Image

from app.models import Car, Photo, PhotoStatus, PhotoType, Show, TransferMethod
from app.services.photo_ingest import (
    get_car_photo,
    ingest_photo_file,
    list_unmatched_photos,
    parse_photo_filename,
    resolve_unmatched_photo,
)


@pytest.fixture()
def show_with_car(db_session):
    show = Show(name="Fall Cruise-In", event_date=date(2026, 9, 1), location="Field")
    db_session.add(show)
    db_session.commit()
    car = Car(show_id=show.id, entry_number="142", data_revision_at_change=1)
    db_session.add(car)
    db_session.commit()
    return show, car


@pytest.fixture(autouse=True)
def _photos_dir(tmp_path, monkeypatch):
    """Every ingest test writes real files — point PHOTOS_DIR at a throwaway
    directory instead of the real one, in every module that imported it by
    value (photo_ingest.py holds its own reference from `from app.config
    import PHOTOS_DIR`, so config.PHOTOS_DIR alone isn't enough)."""
    photos_dir = tmp_path / "photos"
    photos_dir.mkdir()
    monkeypatch.setattr("app.services.photo_ingest.PHOTOS_DIR", photos_dir)
    return photos_dir


def _make_source_jpeg(tmp_path: Path, filename: str, size=(20, 20), color=(200, 50, 50)) -> Path:
    path = tmp_path / filename
    Image.new("RGB", size, color).save(path, "JPEG")
    return path


# ---------------------------------------------------------------------
# Filename parsing
# ---------------------------------------------------------------------

@pytest.mark.parametrize(
    "filename,expected_entry,expected_type",
    [
        ("142_car.jpg", "142", PhotoType.CAR),
        ("142_sheet.jpg", "142", PhotoType.JUDGE_SHEET),
        ("142_CAR.JPG", "142", PhotoType.CAR),  # case insensitive
        ("142_car.jpeg", "142", PhotoType.CAR),  # .jpeg tolerated
        ("AB_12_car.jpg", "AB_12", PhotoType.CAR),  # embedded underscore
    ],
)
def test_parse_photo_filename_matches(filename, expected_entry, expected_type):
    result = parse_photo_filename(filename)
    assert result == (expected_entry, expected_type)


@pytest.mark.parametrize("filename", ["142_car.png", "random.jpg", "142.jpg", "142_car"])
def test_parse_photo_filename_rejects_non_matching(filename):
    assert parse_photo_filename(filename) is None


# ---------------------------------------------------------------------
# ingest_photo_file — matched / unmatched / duplicate
# ---------------------------------------------------------------------

def test_matched_photo_is_copied_never_moved(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "142_car.jpg")

    result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)

    assert result.status == "matched"
    assert result.photo is not None
    assert result.photo.status == PhotoStatus.MATCHED
    assert result.photo.car_id == car.id
    assert source.exists()  # the original is NEVER removed — copy, not move

    dest = _photos_dir / str(show.id) / "142" / "car.jpg"
    assert dest.exists()


def test_thumbnail_is_generated(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "142_car.jpg")

    result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)

    assert result.photo.thumbnail_path is not None
    assert (_photos_dir / result.photo.thumbnail_path).exists()


def test_no_matching_car_is_held_unmatched(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "999_car.jpg")

    result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)

    assert result.status == "unmatched"
    assert result.photo.status == PhotoStatus.UNMATCHED
    assert result.photo.car_id is None
    assert (_photos_dir / str(show.id) / "_unmatched").exists()
    assert source.exists()  # still never deleted


def test_second_photo_for_same_car_and_type_is_kept_as_duplicate(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    # Two DIFFERENT source directories, both containing a file that maps to
    # the exact same {entry_number}_car.jpg destination — same filename
    # required for the naming convention to match, so they can't share a
    # directory. Different size so the second isn't seen as a re-scan of
    # the first (see _find_already_imported).
    first_dir = tmp_path / "card1"
    second_dir = tmp_path / "card2"
    first_dir.mkdir()
    second_dir.mkdir()
    first = _make_source_jpeg(first_dir, "142_car.jpg", size=(20, 20), color=(10, 10, 10))
    second = _make_source_jpeg(second_dir, "142_car.jpg", size=(40, 40), color=(250, 250, 250))

    first_result = ingest_photo_file(db_session, first, show.id, TransferMethod.SD_CARD)
    second_result = ingest_photo_file(db_session, second, show.id, TransferMethod.SD_CARD)

    assert first_result.status == "matched"
    assert second_result.status == "duplicate"
    assert second_result.photo.status == PhotoStatus.DUPLICATE

    photos = list(db_session.query(Photo).filter(Photo.car_id == car.id))
    assert len(photos) == 2  # both kept — never overwritten


def test_rescanning_the_same_file_is_a_no_op_not_a_duplicate(db_session, show_with_car, tmp_path, _photos_dir):
    """A judge's card gets reinserted all day — re-ingesting the exact
    same file (same name, same size) must not manufacture a duplicate."""
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "142_car.jpg")

    ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)
    second_result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)

    assert second_result.status == "skipped"
    photos = list(db_session.query(Photo).filter(Photo.car_id == car.id))
    assert len(photos) == 1


def test_non_matching_filename_is_skipped_without_writing_anything(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "not_a_photo.jpg")

    result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)

    assert result.status == "skipped"
    assert result.photo is None
    assert list(db_session.query(Photo)) == []


def test_transfer_method_is_recorded(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "142_sheet.jpg")

    result = ingest_photo_file(db_session, source, show.id, TransferMethod.WIFI)

    assert result.photo.transfer_method == TransferMethod.WIFI


# ---------------------------------------------------------------------
# Unmatched resolution
# ---------------------------------------------------------------------

def test_resolve_unmatched_photo_moves_it_to_the_assigned_car(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "999_sheet.jpg")
    result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)
    assert result.status == "unmatched"

    resolved = resolve_unmatched_photo(db_session, result.photo.id, car.id)

    assert resolved.status == PhotoStatus.MATCHED
    assert resolved.car_id == car.id
    assert resolved.entry_number == "142"
    assert list_unmatched_photos(db_session, show.id) == []
    assert (_photos_dir / str(show.id) / "142" / "judge_sheet.jpg").exists()


def test_resolve_unmatched_photo_is_idempotent(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    source = _make_source_jpeg(tmp_path, "999_sheet.jpg")
    result = ingest_photo_file(db_session, source, show.id, TransferMethod.SD_CARD)

    first = resolve_unmatched_photo(db_session, result.photo.id, car.id)
    second = resolve_unmatched_photo(db_session, result.photo.id, car.id)

    assert first.file_path == second.file_path
    photos = list(db_session.query(Photo))
    assert len(photos) == 1  # never duplicated by a second resolve


# ---------------------------------------------------------------------
# get_car_photo
# ---------------------------------------------------------------------

def test_get_car_photo_prefers_matched_over_duplicate(db_session, show_with_car, tmp_path, _photos_dir):
    show, car = show_with_car
    first = _make_source_jpeg(tmp_path, "142_car.jpg", color=(10, 10, 10))
    second = tmp_path / "142_car_dup.jpg"
    Image.new("RGB", (40, 40), (250, 250, 250)).save(second, "JPEG")

    ingest_photo_file(db_session, first, show.id, TransferMethod.SD_CARD)
    ingest_photo_file(db_session, second, show.id, TransferMethod.SD_CARD)

    photo = get_car_photo(db_session, car.id, PhotoType.CAR)
    assert photo.status == PhotoStatus.MATCHED
