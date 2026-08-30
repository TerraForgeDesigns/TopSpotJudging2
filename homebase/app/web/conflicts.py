from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import conflicts as conflicts_service
from app.services import cars as cars_service
from app.services import criteria as criteria_service
from app.services.sync import list_unmatched_submissions
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/conflicts")
def conflicts_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    conflicted_cars = conflicts_service.list_conflicted_cars(db, show.id)
    context = base_context(request, db, "/conflicts")
    context["conflict_views"] = [conflicts_service.build_conflict_view(car) for car in conflicted_cars]
    context["all_criteria"] = criteria_service.list_criteria(db, show.id)
    context["unmatched"] = list_unmatched_submissions(db, show.id)
    context["cars"] = cars_service.list_cars(db, show.id)
    return templates.TemplateResponse(request, "conflicts/index.html", context)


@router.post("/conflicts/{car_id}/accept")
def accept_submission(car_id: int, submission_id: int = Form(...), db: Session = Depends(get_db)):
    conflicts_service.accept_submission(db, car_id, submission_id)
    return RedirectResponse("/conflicts", status_code=303)


@router.post("/conflicts/{car_id}/correct")
async def correct_submission(car_id: int, request: Request, db: Session = Depends(get_db)):
    form = await request.form()
    note = str(form.get("note", ""))
    scores: dict[int, int] = {}
    for key, value in form.multi_items():
        if key.startswith("score_") and str(value).strip() != "":
            criteria_id = int(key.removeprefix("score_"))
            scores[criteria_id] = int(value)
    conflicts_service.create_corrected_submission(db, car_id, scores, note)
    return RedirectResponse("/conflicts", status_code=303)


@router.post("/conflicts/unmatched/{submission_id}/assign")
def assign_unmatched(submission_id: int, car_id: int = Form(...), db: Session = Depends(get_db)):
    conflicts_service.assign_unmatched_submission(db, submission_id, car_id)
    return RedirectResponse("/conflicts", status_code=303)


@router.post("/conflicts/unmatched/{submission_id}/discard")
def discard_unmatched(submission_id: int, db: Session = Depends(get_db)):
    conflicts_service.discard_unmatched_submission(db, submission_id)
    return RedirectResponse("/conflicts", status_code=303)
