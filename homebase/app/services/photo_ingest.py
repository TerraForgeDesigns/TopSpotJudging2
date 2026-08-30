"""
The ONE shared photo ingest path — used by both the USB watcher
(services/usb_watcher.py) and the WiFi fallback endpoint
(api/photos.py). See PROTOCOL.md: "[WiFi upload] shares the same
server-side ingest logic as the USB path — both end up writing the same
file naming convention... into the same photo store."

Hard rules (CONTEXT.md — photos are irreplaceable after the show ends):
  - The original source file is only ever copied, never moved or deleted.
  - An existing photo is never overwritten. A second photo for the same
    car+type is kept alongside the first, flagged as a duplicate.
  - A registration number that matches no car is never rejected — it's
    held under photos/<show_id>/_unmatched/ for the host to resolve.
  - A thumbnail failure never fails the ingest — the original copy is
    what matters; thumbnail_path is just left null.
"""
import re
import shutil
import uuid
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, UnidentifiedImageError
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.config import PHOTOS_DIR
from app.models import Car, Photo, PhotoStatus, PhotoType, TransferMethod

_FILENAME_RE = re.compile(r"^(?P<reg>.+)_(?P<kind>car|sheet)$", re.IGNORECASE)
_ALLOWED_EXTENSIONS = {".jpg", ".jpeg"}
THUMBNAIL_MAX_SIZE = (400, 400)


def parse_photo_filename(filename: str) -> tuple[str, PhotoType] | None:
    """Returns (registration_number, photo_type), or None if `filename`
    doesn't match {registration_number}_car.jpg / _sheet.jpg — case
    insensitive, .jpeg tolerated. Greedy matching on the registration
    number means an embedded underscore (e.g. "AB_12_car.jpg") still
    resolves correctly, since re backtracks to the LAST _car/_sheet."""
    suffix = Path(filename).suffix.lower()
    if suffix not in _ALLOWED_EXTENSIONS:
        return None
    stem = Path(filename).stem
    match = _FILENAME_RE.match(stem)
    if not match:
        return None
    registration_number = match.group("reg").strip()
    if not registration_number:
        return None
    photo_type = PhotoType.CAR if match.group("kind").lower() == "car" else PhotoType.JUDGE_SHEET
    return registration_number, photo_type


def _type_slug(photo_type: PhotoType) -> str:
    return "car" if photo_type == PhotoType.CAR else "judge_sheet"


def _unique_path(directory: Path, stem: str, suffix: str = ".jpg") -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    candidate = directory / f"{stem}{suffix}"
    if not candidate.exists():
        return candidate
    return directory / f"{stem}_{uuid.uuid4().hex[:8]}{suffix}"


def _find_already_imported(directory: Path, prefix: str, size: int) -> Path | None:
    """A judge's SD card gets reinserted all day — matching on file size
    against everything already sitting in this slot stops a re-scan from
    manufacturing a fresh "duplicate" for a file we already have."""
    if not directory.exists():
        return None
    for candidate in directory.glob(f"{prefix}*.jpg"):
        if candidate.stem.endswith("_thumb"):
            continue
        if candidate.stat().st_size == size:
            return candidate
    return None


def _generate_thumbnail(source: Path, dest: Path) -> bool:
    try:
        with Image.open(source) as img:
            img = img.convert("RGB")
            img.thumbnail(THUMBNAIL_MAX_SIZE)
            img.save(dest, "JPEG", quality=85)
        return True
    except (UnidentifiedImageError, OSError):
        return False


@dataclass
class IngestResult:
    status: str  # "matched" | "duplicate" | "unmatched" | "skipped" | "error"
    message: str
    photo: Photo | None = None
    registration_number: str | None = None
    source_filename: str = ""


def ingest_photo_file(
    db: Session, source_path: Path, show_id: int, transfer_method: TransferMethod
) -> IngestResult:
    source_filename = source_path.name
    parsed = parse_photo_filename(source_filename)
    if parsed is None:
        return IngestResult(
            status="skipped",
            message=f"'{source_filename}' doesn't match the {{registration_number}}_car/_sheet.jpg pattern.",
            source_filename=source_filename,
        )

    registration_number, photo_type = parsed
    type_slug = _type_slug(photo_type)

    try:
        source_size = source_path.stat().st_size
    except OSError as exc:
        return IngestResult(
            status="error",
            message=f"Couldn't read '{source_filename}': {exc}",
            registration_number=registration_number,
            source_filename=source_filename,
        )

    car = db.scalars(
        select(Car).where(Car.show_id == show_id, Car.registration_number == registration_number)
    ).first()

    if car is None:
        dest_dir = PHOTOS_DIR / str(show_id) / "_unmatched"
        already = _find_already_imported(dest_dir, f"{registration_number}_{type_slug}", source_size)
        if already is not None:
            return IngestResult(
                status="skipped",
                message=f"Already imported as '{already.name}'.",
                registration_number=registration_number,
                source_filename=source_filename,
            )
        dest_path = _unique_path(dest_dir, f"{registration_number}_{type_slug}")
        status = "unmatched"
        car_id = None
    else:
        dest_dir = PHOTOS_DIR / str(show_id) / registration_number
        already = _find_already_imported(dest_dir, type_slug, source_size)
        if already is not None:
            return IngestResult(
                status="skipped",
                message=f"Already imported as '{already.name}'.",
                registration_number=registration_number,
                source_filename=source_filename,
            )
        primary_path = dest_dir / f"{type_slug}.jpg"
        if primary_path.exists():
            dest_path = _unique_path(dest_dir, f"{type_slug}_duplicate")
            status = "duplicate"
        else:
            dest_dir.mkdir(parents=True, exist_ok=True)
            dest_path = primary_path
            status = "matched"
        car_id = car.id

    try:
        shutil.copy2(source_path, dest_path)
    except OSError as exc:
        return IngestResult(
            status="error",
            message=f"Couldn't copy '{source_filename}': {exc}",
            registration_number=registration_number,
            source_filename=source_filename,
        )

    thumb_path = dest_path.with_name(dest_path.stem + "_thumb.jpg")
    thumb_ok = _generate_thumbnail(dest_path, thumb_path)

    photo = Photo(
        show_id=show_id,
        car_id=car_id,
        registration_number=registration_number,
        photo_type=photo_type,
        status=PhotoStatus[status.upper()],
        transfer_method=transfer_method,
        file_path=dest_path.relative_to(PHOTOS_DIR).as_posix(),
        thumbnail_path=thumb_path.relative_to(PHOTOS_DIR).as_posix() if thumb_ok else None,
        original_filename=source_filename,
    )
    db.add(photo)
    db.commit()
    db.refresh(photo)

    messages = {
        "matched": "Imported.",
        "duplicate": f"A {type_slug.replace('_', ' ')} photo already exists for this car — kept both, needs host resolution.",
        "unmatched": f"No car found for registration number '{registration_number}' — held for manual resolution.",
    }
    return IngestResult(
        status=status,
        message=messages[status],
        photo=photo,
        registration_number=registration_number,
        source_filename=source_filename,
    )


def resolve_unmatched_photo(db: Session, photo_id: int, car_id: int) -> Photo:
    """Host manually assigns an unmatched photo to a car (see
    web/photos.py). Reuses the same never-overwrite duplicate handling as
    a fresh ingest — if the target car+type slot is already filled, this
    becomes a duplicate rather than clobbering what's there."""
    photo = db.get(Photo, photo_id)
    if photo is None:
        raise ValueError(f"No photo with id {photo_id}")
    car = db.get(Car, car_id)
    if car is None:
        raise ValueError(f"No car with id {car_id}")

    if photo.status != PhotoStatus.UNMATCHED:
        # Already resolved — a double-submitted form (double-click, browser
        # back-and-resubmit) must be a no-op, not a second move that
        # shuffles an already-correctly-placed file into a spurious
        # "_duplicate" of itself.
        return photo

    old_path = PHOTOS_DIR / photo.file_path
    old_thumb_path = PHOTOS_DIR / photo.thumbnail_path if photo.thumbnail_path else None

    type_slug = _type_slug(photo.photo_type)
    dest_dir = PHOTOS_DIR / str(photo.show_id) / car.registration_number
    primary_path = dest_dir / f"{type_slug}.jpg"
    if primary_path.exists():
        new_path = _unique_path(dest_dir, f"{type_slug}_duplicate")
        new_status = PhotoStatus.DUPLICATE
    else:
        dest_dir.mkdir(parents=True, exist_ok=True)
        new_path = primary_path
        new_status = PhotoStatus.MATCHED

    shutil.move(str(old_path), str(new_path))
    new_thumb_path = None
    if old_thumb_path and old_thumb_path.exists():
        new_thumb_path = new_path.with_name(new_path.stem + "_thumb.jpg")
        shutil.move(str(old_thumb_path), str(new_thumb_path))

    photo.car_id = car.id
    photo.registration_number = car.registration_number
    photo.status = new_status
    photo.file_path = new_path.relative_to(PHOTOS_DIR).as_posix()
    photo.thumbnail_path = new_thumb_path.relative_to(PHOTOS_DIR).as_posix() if new_thumb_path else None
    db.commit()
    db.refresh(photo)
    return photo


def list_unmatched_photos(db: Session, show_id: int) -> list[Photo]:
    return list(
        db.scalars(
            select(Photo)
            .where(Photo.show_id == show_id, Photo.status == PhotoStatus.UNMATCHED)
            .order_by(Photo.transferred_at.desc())
        )
    )


def find_candidate_photo_files(directory: Path) -> list[Path]:
    """Files in `directory` whose name matches the naming pattern — used to
    compute an accurate progress total before scanning starts."""
    if not directory.exists():
        return []
    return [p for p in directory.iterdir() if p.is_file() and parse_photo_filename(p.name) is not None]
