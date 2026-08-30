"""
The handheld-facing sync API — see /PROTOCOL.md. This module is a thin
wire-format wrapper; all business logic lives in services/sync.py.
"""
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.orm import Session

from app.api.schemas import (
    HealthResponse,
    ResultItem,
    RosterResponse,
    SubmissionsRequest,
    SubmissionsResponse,
)
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
    return HealthResponse(server_time=_server_time(), active_show=show.name if show else None)


@router.get("/sync/roster", response_model=RosterResponse)
def get_roster(
    request: Request,
    handheld_id: str,
    since: datetime | None = None,
    db: Session = Depends(get_db),
):
    show = _require_active_show(db)

    handheld = sync_service.get_or_create_handheld(db, handheld_id)
    client_ip = request.client.host if request.client else None
    sync_service.touch_handheld_sync(db, handheld, client_ip)

    snapshot = sync_service.get_roster_snapshot(db, show, since)
    return RosterResponse(server_time=_server_time(), **snapshot)


@router.post("/sync/submissions", response_model=SubmissionsResponse)
def post_submissions(payload: SubmissionsRequest, request: Request, db: Session = Depends(get_db)):
    show = _require_active_show(db)

    handheld = sync_service.get_or_create_handheld(db, payload.handheld_id)
    client_ip = request.client.host if request.client else None
    sync_service.touch_handheld_sync(db, handheld, client_ip)

    results = [
        sync_service.process_submission(db, show, handheld, item) for item in payload.submissions
    ]

    roster_delta_dict = sync_service.get_roster_snapshot(db, show, payload.since)

    return SubmissionsResponse(
        server_time=_server_time(),
        results=[
            ResultItem(registration_number=r.registration_number, status=r.status, message=r.message)
            for r in results
        ],
        roster_delta=RosterResponse(server_time=_server_time(), **roster_delta_dict),
        summary=roster_delta_dict["summary"],
    )
