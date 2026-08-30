"""
Conflict resolution — see CONTEXT.md and services/conflicts.py. A car
gets flagged when two handhelds both produce an accepted-looking
submission for the same entry; the host resolves it here by picking one
submission or entering a corrected score set.
"""
from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services.cars import get_car
from app.services.conflicts import accept_submission, build_conflict_view, create_corrected_submission, list_conflicted_cars
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/conflicts")
def conflicts_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/conflicts")
    context["cars"] = list_conflicted_cars(db, show.id)
    return templates.TemplateResponse(request, "conflicts/index.html", context)


@router.get("/conflicts/{car_id}")
def conflict_detail(car_id: int, request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = get_car(db, car_id)
    if car is None or car.show_id != show.id:
        return RedirectResponse("/conflicts", status_code=303)

    context = base_context(request, db, "/conflicts")
    context["view"] = build_conflict_view(car)
    context["correction_error"] = None
    return templates.TemplateResponse(request, "conflicts/detail.html", context)


@router.post("/conflicts/{car_id}/accept")
def accept(car_id: int, submission_id: int = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = get_car(db, car_id)
    if car is None or car.show_id != show.id:
        return RedirectResponse("/conflicts", status_code=303)

    accept_submission(db, show, car, submission_id)
    return RedirectResponse("/conflicts", status_code=303)


@router.post("/conflicts/{car_id}/correct")
async def correct(car_id: int, request: Request, note: str = Form(""), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = get_car(db, car_id)
    if car is None or car.show_id != show.id:
        return RedirectResponse("/conflicts", status_code=303)

    view = build_conflict_view(car)
    form = await request.form()
    scores: dict[int, int] = {}
    errors: dict[int, str] = {}
    for category in view.categories:
        raw = (form.get(f"score_{category.id}") or "").strip()
        if not raw.isdigit():
            errors[category.id] = f"Enter a score for {category.name}."
            continue
        points = int(raw)
        if points < 1 or points > show.score_range_max:
            errors[category.id] = f"{category.name} must be between 1 and {show.score_range_max}."
            continue
        scores[category.id] = points

    if errors:
        context = base_context(request, db, "/conflicts")
        context["view"] = view
        context["correction_error"] = "Fix the highlighted scores before saving the correction."
        context["score_errors"] = errors
        return templates.TemplateResponse(request, "conflicts/detail.html", context)

    create_corrected_submission(db, show, car, scores, note)
    return RedirectResponse("/conflicts", status_code=303)
