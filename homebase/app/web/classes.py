from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import car_classes as car_classes_service
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/classes")
def list_classes(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/classes")
    context["classes_with_counts"] = car_classes_service.list_classes_with_counts(db, show.id)
    return templates.TemplateResponse(request, "classes/list.html", context)


@router.post("/classes")
def create_class(name: str = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)
    if name.strip():
        car_classes_service.create_class(db, show.id, name)
    return RedirectResponse("/classes", status_code=303)


@router.post("/classes/{class_id}/rename")
def rename_class(class_id: int, name: str = Form(...), db: Session = Depends(get_db)):
    if name.strip():
        car_classes_service.rename_class(db, class_id, name)
    return RedirectResponse("/classes", status_code=303)


@router.post("/classes/{class_id}/move")
def move_class(class_id: int, direction: str = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is not None and direction in ("up", "down"):
        car_classes_service.move_class(db, show.id, class_id, direction)
    return RedirectResponse("/classes", status_code=303)


@router.post("/classes/{class_id}/delete")
def delete_class(class_id: int, db: Session = Depends(get_db)):
    car_classes_service.delete_class(db, class_id)
    return RedirectResponse("/classes", status_code=303)
