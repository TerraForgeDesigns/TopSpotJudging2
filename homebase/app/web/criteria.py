from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import criteria as criteria_service
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/criteria")
def list_criteria(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/criteria")
    context["criteria_list"] = criteria_service.list_criteria(db, show.id)
    return templates.TemplateResponse(request, "criteria/list.html", context)


@router.post("/criteria")
def create_criteria(name: str = Form(...), max_points: int = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is not None and name.strip():
        criteria_service.create_criteria(db, show.id, name, max_points)
    return RedirectResponse("/criteria", status_code=303)


@router.post("/criteria/{criteria_id}")
def update_criteria(
    criteria_id: int, name: str = Form(...), max_points: int = Form(...), db: Session = Depends(get_db)
):
    if name.strip():
        criteria_service.update_criteria(db, criteria_id, name, max_points)
    return RedirectResponse("/criteria", status_code=303)


@router.post("/criteria/{criteria_id}/move")
def move_criteria(criteria_id: int, direction: str = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is not None and direction in ("up", "down"):
        criteria_service.move_criteria(db, show.id, criteria_id, direction)
    return RedirectResponse("/criteria", status_code=303)


@router.post("/criteria/{criteria_id}/toggle-active")
def toggle_active(criteria_id: int, active: bool = Form(...), db: Session = Depends(get_db)):
    criteria_service.set_active(db, criteria_id, active)
    return RedirectResponse("/criteria", status_code=303)
