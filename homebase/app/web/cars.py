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
from app.services.manual_judging import get_manual_judge_context, manual_judge_car
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


@router.get("/cars/{car_id}/judge")
def manual_judge_form(
    car_id: int,
    request: Request,
    replace: bool = False,
    db: Session = Depends(get_db),
):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    ctx = get_manual_judge_context(db, show, car_id)
    if ctx is None:
        return RedirectResponse("/cars", status_code=303)

    context = base_context(request, db, "/cars")
    context["ctx"] = ctx
    context["replace"] = replace
    context["error"] = ""
    context["score_values"] = {}
    context["selected_awards"] = set()
    return templates.TemplateResponse(request, "cars/manual_judge.html", context)


@router.post("/cars/{car_id}/judge")
async def manual_judge_submit(
    car_id: int,
    request: Request,
    db: Session = Depends(get_db),
):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    form = await request.form()
    replace = form.get("replace_existing") == "1"
    scores: dict[int, int] = {}
    for key, value in form.multi_items():
        if key.startswith("score_"):
            try:
                scores[int(key.removeprefix("score_"))] = int(str(value))
            except ValueError:
                continue

    nomination_ids: list[int] = []
    for value in form.getlist("nominations"):
        try:
            nomination_ids.append(int(str(value)))
        except ValueError:
            continue

    result = manual_judge_car(
        db,
        show,
        car_id,
        scores_by_category_id=scores,
        nomination_ids=nomination_ids,
        participant=str(form.get("participant", "")),
        year=str(form.get("year", "")),
        make=str(form.get("make", "")),
        model=str(form.get("model", "")),
        operator_label=str(form.get("operator_label", "")),
        replace_existing=replace,
    )
    if result.ok:
        return RedirectResponse("/cars", status_code=303)

    ctx = get_manual_judge_context(db, show, car_id)
    if ctx is None:
        return RedirectResponse("/cars", status_code=303)
    context = base_context(request, db, "/cars")
    context["ctx"] = ctx
    context["replace"] = replace
    context["error"] = result.message
    context["score_values"] = scores
    context["selected_awards"] = set(nomination_ids)
    return templates.TemplateResponse(request, "cars/manual_judge.html", context, status_code=400)
