import threading
from collections import defaultdict

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import PhotoType
from app.services import photo_import_status as status_service
from app.services.cars import list_cars
from app.services.photo_ingest import list_unmatched_photos, resolve_unmatched_photo
from app.services.sd_card_watcher import list_removable_drives, scan_and_ingest_drive
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/photos")
def photos_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/photos")
    context["unmatched_count"] = len(list_unmatched_photos(db, show.id))
    context["known_drives"] = list_removable_drives()
    context["status"] = status_service.get_status()
    return templates.TemplateResponse(request, "photos/index.html", context)


@router.get("/photos/live")
def photos_live(request: Request):
    context = {"request": request, "status": status_service.get_status()}
    return templates.TemplateResponse(request, "partials/photos_live.html", context)


@router.post("/photos/scan")
def photos_scan(db: Session = Depends(get_db)):
    """Manual trigger — the watcher already does this automatically on
    mount, but the host shouldn't be stuck waiting on poll timing (or
    replugging a drive) to prove it's working."""
    drives = list_removable_drives()
    for drive in drives:
        threading.Thread(
            target=scan_and_ingest_drive, args=(drive, str(drive)), daemon=True, name="sd-card-manual-scan"
        ).start()
    return RedirectResponse("/photos", status_code=303)


@router.get("/photos/unmatched")
def photos_unmatched(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    photos = list_unmatched_photos(db, show.id)
    groups: dict[str, list] = defaultdict(list)
    for photo in photos:
        groups[photo.entry_number].append(photo)
    # Judge sheet first within each group — that's usually where the
    # entry number is handwritten, so it's the host's best clue.
    grouped = [
        {"entry_number": entry_number, "photos": sorted(items, key=lambda p: p.photo_type != PhotoType.JUDGE_SHEET)}
        for entry_number, items in groups.items()
    ]

    context = base_context(request, db, "/photos")
    context["groups"] = grouped
    context["cars"] = [row.car for row in list_cars(db, show.id)]
    return templates.TemplateResponse(request, "photos/unmatched.html", context)


@router.post("/photos/resolve")
def resolve_photos(photo_ids: list[int] = Form(...), car_id: int = Form(...), db: Session = Depends(get_db)):
    for photo_id in photo_ids:
        resolve_unmatched_photo(db, photo_id, car_id)
    return RedirectResponse("/photos/unmatched", status_code=303)
