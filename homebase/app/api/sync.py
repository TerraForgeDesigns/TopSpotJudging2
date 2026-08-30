"""
The handheld-facing API — see /PROTOCOL.md. This module is a thin
wire-format wrapper; all business logic lives in services/sync.py.

The old two-endpoint protocol (GET /sync/roster, POST /sync/submissions)
was replaced by this single POST /sync as part of the Aug 2026 spec
update — see DECISIONS.md for why two revision counters replaced
timestamp-based deltas.
"""
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.orm import Session

from app.api.schemas import HealthResponse, ResultItem, SyncRequest, SyncResponse
from app.db import get_db
from app.models import Show
from app.services import sync as sync_service
from app.services.shows import get_active_show

router = APIRouter()


def _server_time() -> datetime:
    """The PC's local time, timezone-aware — handhelds set their clock from
    this (see CONTEXT.md/PROTOCOL.md: home base's clock is the sole time
    authority, no NTP anywhere in this system)."""
    return datetime.now().astimezone()


def _require_active_show(db: Session) -> Show:
    show = get_active_show(db)
    if show is None:
        raise HTTPException(status_code=409, detail="No active show configured on home base.")
    return show


@router.get("/health", response_model=HealthResponse)
def health(db: Session = Depends(get_db)):
    show = get_active_show(db)
    return HealthResponse(server_time=_server_time(), show_name=show.name if show else None)


@router.post("/sync", response_model=SyncResponse)
def sync(payload: SyncRequest, request: Request, db: Session = Depends(get_db)):
    show = _require_active_show(db)

    handheld = sync_service.get_or_create_handheld(db, payload.handheld_id)
    client_ip = request.client.host if request.client else None
    sync_service.touch_handheld_sync(
        db, handheld, client_ip, payload.battery_pct, payload.config_revision, payload.data_revision
    )

    results = [sync_service.process_submission(db, show, handheld, item) for item in payload.submissions]

    # Refresh — process_submission may have bumped show_data_revision.
    db.refresh(show)

    configuration = None
    if payload.config_revision < show.configuration_revision:
        configuration = sync_service.get_configuration(db, show)

    cars = []
    if payload.data_revision < show.show_data_revision:
        cars = sync_service.get_cars_delta(db, show, payload.data_revision)

    return SyncResponse(
        server_time=_server_time(),
        config_revision=show.configuration_revision,
        data_revision=show.show_data_revision,
        configuration=configuration,
        cars=cars,
        vehicle_additions=[],
        results=[ResultItem(entry_number=r.entry_number, status=r.status, message=r.message) for r in results],
        summary=sync_service.get_protocol_summary(db, show.id),
    )
