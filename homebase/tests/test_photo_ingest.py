from datetime import date
from pathlib import Path

import pytest
from PIL import Image

from app.models import PhotoStatus, PhotoType, TransferMethod
from app.services import cars as cars_service
from app.services import photo_ingest
from app.services import shows as shows_service


def _make_show(db_session):
    return shows_service.create_show(db_session, "Fall Cruise-In", date(2026, 9, 12), "Downtown Lot")


def _make_car(db_session, show, reg="0142"):
    return cars_service.create_car(db_session, show.id, reg, reg, "Chevrolet", "Camaro", 1967, None)


def _make_jpeg(path: Path, size=(20, 20)) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", size, color=(120, 80, 40)).save(path, "JPEG")
    return path


@pytest.fixture()
def photos_dir(tmp_path, monkeypatch):
    directory = tmp_path / "photos"
    directory.mkdir()
    monkeypatch.setattr(photo_ingest, "PHOTOS_DIR", directory)
    return directory


# ---------------------------------------------------------------------------
# Filename parsing
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "filename,expected",
    [
        ("0142_car.jpg", ("0142", PhotoType.CAR)),
        ("0142_sheet.jpg", ("0142", PhotoType.JUDGE_SHEET)),
        ("0142_CAR.JPG", ("0142", PhotoType.CAR)),  # case-insensitive
        ("0142_sheet.jpeg", ("0142", PhotoType.JUDGE_SHEET)),  # .jpeg tolerated
        ("AB_12_car.jpg", ("AB_12", PhotoType.CAR)),  # embedded underscore in reg #
        ("random_photo.png", None),  # wrong extension
        ("0142.jpg", None),  # no _car/_sheet suffix
        ("0142_dashboard.jpg", None),  # unrelated suffix
    ],
)
def test_parse_photo_filename(filename, expected):
    assert photo_ingest.parse_photo_filename(filename) == expected


# ---------------------------------------------------------------------------
# ingest_photo_file
# ---------------------------------------------------------------------------


def test_ingest_matched_photo_creates_row_and_thumbnail(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    source = _make_jpeg(tmp_path / "incoming" / "0142_car.jpg")

    result = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)

    assert result.status == "matched"
    assert result.photo.status == PhotoStatus.MATCHED
    assert result.photo.file_path == f"{show.id}/0142/car.jpg"
    assert (photos_dir / result.photo.file_path).exists()
    assert result.photo.thumbnail_path is not None
    assert (photos_dir / result.photo.thumbnail_path).exists()
    # source file must still exist — copy, never move
    assert source.exists()


def test_ingest_unmatched_photo_is_held_not_rejected(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    # deliberately no car "9999"
    source = _make_jpeg(tmp_path / "incoming" / "9999_car.jpg")

    result = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)

    assert result.status == "unmatched"
    assert result.photo.status == PhotoStatus.UNMATCHED
    assert result.photo.car_id is None
    assert result.photo.registration_number == "9999"
    assert f"{show.id}/_unmatched/" in result.photo.file_path
    assert (photos_dir / result.photo.file_path).exists()


def test_ingest_duplicate_never_overwrites_original(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    first_source = _make_jpeg(tmp_path / "incoming" / "0142_car.jpg", size=(20, 20))
    second_source = _make_jpeg(tmp_path / "incoming2" / "0142_car.jpg", size=(50, 50))

    first = photo_ingest.ingest_photo_file(db_session, first_source, show.id, TransferMethod.USB)
    second = photo_ingest.ingest_photo_file(db_session, second_source, show.id, TransferMethod.USB)

    assert first.status == "matched"
    assert second.status == "duplicate"
    # original file untouched
    original_path = photos_dir / first.photo.file_path
    assert original_path.exists()
    assert original_path.stat().st_size == (photos_dir / f"{show.id}/0142/car.jpg").stat().st_size
    # both rows exist, pointing at different files
    assert first.photo.file_path != second.photo.file_path
    assert (photos_dir / second.photo.file_path).exists()


def test_reinserting_same_card_does_not_create_duplicate(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")
    source = _make_jpeg(tmp_path / "incoming" / "0142_car.jpg", size=(20, 20))

    first = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)
    # same SD card, same file, scanned again after a reinsertion
    second = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)

    assert first.status == "matched"
    assert second.status == "skipped"

    from app.models import Photo

    all_photos = db_session.query(Photo).filter_by(registration_number="0142").all()
    assert len(all_photos) == 1  # no duplicate row for the re-scan


def test_ingest_skips_files_not_matching_naming_pattern(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    source = _make_jpeg(tmp_path / "incoming" / "IMG_00231.jpg")

    result = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)

    assert result.status == "skipped"
    assert result.photo is None


# ---------------------------------------------------------------------------
# resolve_unmatched_photo
# ---------------------------------------------------------------------------


def test_resolve_unmatched_photo_assigns_car_and_moves_file(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    car = _make_car(db_session, show, "0142")
    source = _make_jpeg(tmp_path / "incoming" / "9999_car.jpg")
    held = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)
    old_path = photos_dir / held.photo.file_path

    resolved = photo_ingest.resolve_unmatched_photo(db_session, held.photo.id, car.id)

    assert resolved.status == PhotoStatus.MATCHED
    assert resolved.car_id == car.id
    assert resolved.registration_number == "0142"
    assert resolved.file_path == f"{show.id}/0142/car.jpg"
    assert (photos_dir / resolved.file_path).exists()
    assert not old_path.exists()  # moved, not copied, within our own managed directory


def test_resolving_an_already_resolved_photo_is_a_no_op(db_session, photos_dir, tmp_path):
    """A double-submitted resolve form (double-click, resubmit) must not
    re-shuffle an already-placed file into a spurious duplicate of itself."""
    show = _make_show(db_session)
    car = _make_car(db_session, show, "0142")
    source = _make_jpeg(tmp_path / "incoming" / "9999_car.jpg")
    held = photo_ingest.ingest_photo_file(db_session, source, show.id, TransferMethod.USB)

    first = photo_ingest.resolve_unmatched_photo(db_session, held.photo.id, car.id)
    second = photo_ingest.resolve_unmatched_photo(db_session, held.photo.id, car.id)

    assert first.status == PhotoStatus.MATCHED
    assert second.status == PhotoStatus.MATCHED
    assert second.file_path == first.file_path
    primary = photos_dir / f"{show.id}/0142/car.jpg"
    assert primary.exists()
    # no spurious "_duplicate" file created by the second call
    assert list((photos_dir / f"{show.id}/0142").glob("car_duplicate*")) == []


def test_resolve_unmatched_photo_becomes_duplicate_if_slot_taken(db_session, photos_dir, tmp_path):
    show = _make_show(db_session)
    car = _make_car(db_session, show, "0142")
    existing_source = _make_jpeg(tmp_path / "incoming" / "0142_car.jpg")
    photo_ingest.ingest_photo_file(db_session, existing_source, show.id, TransferMethod.USB)

    unmatched_source = _make_jpeg(tmp_path / "incoming2" / "9999_car.jpg", size=(99, 99))
    held = photo_ingest.ingest_photo_file(db_session, unmatched_source, show.id, TransferMethod.USB)

    resolved = photo_ingest.resolve_unmatched_photo(db_session, held.photo.id, car.id)

    assert resolved.status == PhotoStatus.DUPLICATE
    primary = photos_dir / f"{show.id}/0142/car.jpg"
    assert primary.exists()  # the original match is still untouched


# ---------------------------------------------------------------------------
# WiFi upload endpoint (PROTOCOL.md POST /photos/upload)
# ---------------------------------------------------------------------------


def test_wifi_upload_matches_existing_car(client, db_session, photos_dir, tmp_path, monkeypatch):
    import app.api.photos as photos_api

    monkeypatch.setattr(photos_api, "PHOTOS_DIR", photos_dir)
    show = _make_show(db_session)
    _make_car(db_session, show, "0142")

    source = _make_jpeg(tmp_path / "upload.jpg")
    with source.open("rb") as f:
        resp = client.post(
            "/api/v1/photos/upload",
            data={"registration_number": "0142", "photo_type": "car"},
            files={"file": ("0142_car.jpg", f, "image/jpeg")},
        )

    assert resp.status_code == 200
    body = resp.json()
    assert body["status"] == "matched"

    from app.models import Photo

    photo = db_session.query(Photo).filter_by(registration_number="0142").one()
    assert photo.transfer_method == TransferMethod.WIFI
    assert (photos_dir / photo.file_path).exists()
