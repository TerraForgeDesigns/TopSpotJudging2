from datetime import date

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import Show
from app.services import shows as shows_service
from app.templating import templates
from app.web.context import base_context

router = APIRouter()


@router.get("/shows")
def list_shows(request: Request, db: Session = Depends(get_db)):
    context = base_context(request, db, "/shows")
    context["shows"] = shows_service.list_shows(db)
    return templates.TemplateResponse(request, "shows/list.html", context)


@router.get("/shows/{show_id}/edit")
def edit_show(show_id: int, request: Request, db: Session = Depends(get_db)):
    context = base_context(request, db, "/shows")
    show = db.get(Show, show_id)
    if show is None:
        return RedirectResponse("/shows", status_code=303)
    context["show"] = show
    return templates.TemplateResponse(request, "shows/edit.html", context)


@router.post("/shows/{show_id}")
def update_show(
    show_id: int,
    name: str = Form(...),
    event_date: date = Form(...),
    location: str = Form(""),
    notes: str = Form(""),
    db: Session = Depends(get_db),
):
    shows_service.update_show(db, show_id, name=name, event_date=event_date, location=location, notes=notes)
    return RedirectResponse("/shows", status_code=303)


@router.post("/shows/{show_id}/activate")
def activate_show(show_id: int, db: Session = Depends(get_db)):
    shows_service.set_active_show(db, show_id)
    return RedirectResponse("/shows", status_code=303)
