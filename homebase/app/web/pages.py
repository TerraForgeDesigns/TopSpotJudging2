from fastapi import APIRouter, Depends, Request
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import dashboard as dashboard_service
from app.templating import templates
from app.web.context import base_context

router = APIRouter()


def _dashboard_live_context(request: Request, db: Session) -> dict:
    context = base_context(request, db, "/")
    show = context["active_show"]
    if show is not None:
        context["summary"] = dashboard_service.get_summary(db, show.id)
        context["handheld_statuses"] = dashboard_service.get_handheld_statuses(db)
    else:
        context["summary"] = None
        context["handheld_statuses"] = []
    return context


@router.get("/")
def dashboard(request: Request, db: Session = Depends(get_db)):
    context = _dashboard_live_context(request, db)
    return templates.TemplateResponse(request, "dashboard.html", context)


@router.get("/dashboard/live")
def dashboard_live(request: Request, db: Session = Depends(get_db)):
    context = _dashboard_live_context(request, db)
    return templates.TemplateResponse(request, "partials/dashboard_live.html", context)


@router.get("/styleguide")
def styleguide(request: Request, db: Session = Depends(get_db)):
    context = base_context(request, db, "/styleguide")
    return templates.TemplateResponse(request, "styleguide.html", context)
