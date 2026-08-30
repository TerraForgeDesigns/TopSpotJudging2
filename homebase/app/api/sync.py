"""
The handheld-facing API — see /PROTOCOL.md.

The old two-endpoint protocol (GET /sync/roster, POST /sync/submissions)
was deleted here as step 1 of the Aug 2026 spec reconciliation — see
DECISIONS.md. Only /health survives from the old wire contract; the
unified POST /sync lands in a later step of this same task.
"""
from datetime import datetime

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from app.api.schemas import HealthResponse
from app.db import get_db
from app.services.shows import get_active_show

router = APIRouter()


def _server_time() -> datetime:
    """The PC's local time, timezone-aware — handhelds set their clock from
    this (see CONTEXT.md/PROTOCOL.md: home base's clock is the sole time
    authority, no NTP anywhere in this system)."""
    return datetime.now().astimezone()


@router.get("/health", response_model=HealthResponse)
def health(db: Session = Depends(get_db)):
    show = get_active_show(db)
    return HealthResponse(server_time=_server_time(), show_name=show.name if show else None)
