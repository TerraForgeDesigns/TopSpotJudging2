from fastapi import Request
from sqlalchemy.orm import Session

from app.models import Show
from app.services.shows import get_active_show
from app.web.nav import NAV_ITEMS


def base_context(request: Request, db: Session, active_path: str) -> dict:
    active_show = get_active_show(db)
    return {
        "request": request,
        "nav_items": NAV_ITEMS,
        "active_path": active_path,
        "active_show": active_show,
        "connection_status": "active" if active_show else "no_show_loaded",
    }


def require_active_show(db: Session) -> Show | None:
    """Every Cars/Classes/Criteria page needs an active show to operate on
    (see CONTEXT.md). Callers redirect to /shows when this is None."""
    return get_active_show(db)
