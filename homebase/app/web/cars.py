"""
The Cars Show Dashboard section — see CONTEXT.md. Viewing/searching the
roster and correcting an individual entry's details. Adding MORE cars to
the show is a separate concern that lives under Edit Show, not here —
see web/shows.py and DECISIONS.md.
"""
from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services.cars import get_car, list_cars, update_car_details
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/cars")
def cars_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/cars")
    context["rows"] = list_cars(db, show.id)
    return templates.TemplateResponse(request, "cars/list.html", context)


@router.get("/cars/{car_id}/edit")
def edit_car_form(car_id: int, request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = get_car(db, car_id)
    if car is None or car.show_id != show.id:
        return RedirectResponse("/cars", status_code=303)

    context = base_context(request, db, "/cars")
    context["car"] = car
    return templates.TemplateResponse(request, "cars/edit.html", context)


@router.post("/cars/{car_id}/edit")
def edit_car_submit(
    car_id: int,
    participant: str = Form(""),
    year: str = Form(""),
    make: str = Form(""),
    model: str = Form(""),
    vehicle_type: str = Form(""),
    db: Session = Depends(get_db),
):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = get_car(db, car_id)
    if car is None or car.show_id != show.id:
        return RedirectResponse("/cars", status_code=303)

    update_car_details(db, show, car, participant, year, make, model, vehicle_type)
    return RedirectResponse("/cars", status_code=303)
