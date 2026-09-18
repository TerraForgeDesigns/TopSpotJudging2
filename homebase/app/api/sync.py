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

from app.api.schemas import HealthResponse, HandshakeRequest, HandshakeResponse, ResultItem, SyncRequest, SyncResponse
from app.config import PROTOCOL_VERSION, SERVICE_NAME
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
    return HealthResponse(
        service=SERVICE_NAME,
        status="ok",
        protocol_version=PROTOCOL_VERSION,
        server_time=_server_time(),
        show_name=show.name if show else None,
    )


@router.post("/handshake", response_model=HandshakeResponse)
def handshake(payload: HandshakeRequest, request: Request, db: Session = Depends(get_db)):
    if payload.protocol_version != PROTOCOL_VERSION:
        raise HTTPException(status_code=426, detail="Unsupported handheld protocol version.")
    handheld = sync_service.get_or_create_handheld(db, payload.handheld_id)
    client_ip = request.client.host if request.client else None
    sync_service.touch_handheld_handshake(db, handheld, client_ip, payload.firmware_version)
    show = get_active_show(db)
    sync_service.logger.info(
        "HANDSHAKE REQUEST: handheld=%s show_id=%s",
        payload.handheld_id,
        show.id if show else None,
    )
    return HandshakeResponse(
        service=SERVICE_NAME,
        status="ok",
        protocol_version=PROTOCOL_VERSION,
        server_time=_server_time(),
        show_name=show.name if show else None,
        handheld_id=handheld.label,
    )


@router.post("/sync", response_model=SyncResponse)
def sync(payload: SyncRequest, request: Request, db: Session = Depends(get_db)):
    if payload.protocol_version is not None and payload.protocol_version != PROTOCOL_VERSION:
        raise HTTPException(status_code=426, detail="Unsupported handheld protocol version.")
    show = _require_active_show(db)

    handheld = sync_service.get_or_create_handheld(db, payload.handheld_id)
    client_ip = request.client.host if request.client else None
    sync_service.touch_handheld_sync(
        db,
        handheld,
        client_ip,
        payload.battery_pct,
        payload.config_revision,
        payload.data_revision,
        payload.firmware_version,
    )

    results = [sync_service.process_submission(db, show, handheld, item) for item in payload.submissions]

    # Refresh — process_submission may have bumped show_data_revision.
    db.refresh(show)
    summary = sync_service.get_protocol_summary(db, show.id)
    authoritative_count = sync_service.get_authoritative_car_count(db, show.id)
    show_changed = payload.show_id != show.id
    full_roster_needed = show_changed or payload.data_revision <= 0 or payload.known_car_count != authoritative_count
    sync_mode = "FULL" if full_roster_needed else "DELTA"

    configuration = None
    if show_changed or payload.config_revision < show.configuration_revision:
        configuration = sync_service.get_configuration(db, show)

    cars = []
    if full_roster_needed:
        cars = sync_service.get_cars_snapshot(db, show)
    elif payload.data_revision < show.show_data_revision:
        cars = sync_service.get_cars_delta(db, show, payload.data_revision)

    sync_service.logger.info(
        "CAR ROSTER: show_id=%s authoritative_count=%s",
        show.id,
        authoritative_count,
    )
    sync_service.logger.info(
        "SYNC DECISION: mode=%s known_car_count=%s authoritative_count=%s cars_sent=%s",
        sync_mode,
        payload.known_car_count,
        authoritative_count,
        len(cars),
    )

    return SyncResponse(
        server_time=_server_time(),
        config_revision=show.configuration_revision,
        data_revision=show.show_data_revision,
        sync_mode=sync_mode,
        configuration=configuration,
        cars=cars,
        vehicle_additions=[],
        results=[ResultItem(entry_number=r.entry_number, status=r.status, message=r.message) for r in results],
        summary=summary,
    )
