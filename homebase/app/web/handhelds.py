"""
The Handhelds Show Dashboard section — see CONTEXT.md's Show Dashboard
list. Same data the Overview panel shows (services/dashboard.py's
get_handheld_statuses), just as its own page for when the host wants
only that.
"""
from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import dashboard as dashboard_service
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/handhelds")
def handhelds_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/handhelds")
    context["handheld_statuses"] = dashboard_service.get_handheld_statuses(db)
    return templates.TemplateResponse(request, "handhelds.html", context)
