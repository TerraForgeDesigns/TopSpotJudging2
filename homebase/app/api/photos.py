"""
WiFi fallback path for photo transfer — see PROTOCOL.md POST /photos/upload.
Writes the upload to a temp file named per the naming convention, then
hands off to the exact same ingest_photo_file() the USB watcher uses
(see services/photo_ingest.py's module docstring).
"""
import shutil
import uuid

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile
from sqlalchemy.orm import Session

from app.config import PHOTOS_DIR
from app.db import get_db
from app.models import TransferMethod
from app.services.photo_ingest import ingest_photo_file
from app.services.shows import get_active_show

router = APIRouter()

# PROTOCOL.md's naming convention uses "_car"/"_sheet" as the type word in
# the filename — that's the only place the protocol spells out these two
# terms, so the wire `photo_type` field uses the same vocabulary rather
# than our internal PhotoType enum's "judge_sheet". See DECISIONS.md.
_TYPE_SUFFIX = {"car": "car", "sheet": "sheet"}


@router.post("/photos/upload")
async def upload_photo(
    registration_number: str = Form(...),
    photo_type: str = Form(...),
    file: UploadFile = File(...),
    db: Session = Depends(get_db),
):
    show = get_active_show(db)
    if show is None:
        raise HTTPException(status_code=409, detail="No active show configured on home base.")

    suffix = _TYPE_SUFFIX.get(photo_type.strip().lower())
    if suffix is None:
        raise HTTPException(status_code=422, detail="photo_type must be 'car' or 'sheet'.")

    registration_number = registration_number.strip()
    incoming_dir = PHOTOS_DIR / "_incoming" / uuid.uuid4().hex
    incoming_dir.mkdir(parents=True, exist_ok=True)
    temp_path = incoming_dir / f"{registration_number}_{suffix}.jpg"

    try:
        with temp_path.open("wb") as out:
            shutil.copyfileobj(file.file, out)
        result = ingest_photo_file(db, temp_path, show.id, TransferMethod.WIFI)
    finally:
        shutil.rmtree(incoming_dir, ignore_errors=True)

    return {
        "status": result.status,
        "message": result.message,
        "registration_number": result.registration_number,
    }
