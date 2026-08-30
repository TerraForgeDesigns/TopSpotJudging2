from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import AwardCategory
from app.services import awards as awards_service
from app.services import car_classes as car_classes_service
from app.services import cars as cars_service
from app.services import criteria as criteria_service
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/awards")
def awards_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    awards = awards_service.list_awards(db, show.id)
    suggestions = {award.id: awards_service.suggest_winner(db, award) for award in awards}

    context = base_context(request, db, "/awards")
    context["awards"] = awards
    context["suggestions"] = suggestions
    context["criteria_list"] = criteria_service.list_criteria(db, show.id)
    context["classes"] = [c for c, _ in car_classes_service.list_classes_with_counts(db, show.id)]
    context["cars"] = cars_service.list_cars(db, show.id)
    context["presentable_count"] = len(awards_service.build_presentation_sequence(db, show.id))
    return templates.TemplateResponse(request, "awards/index.html", context)


@router.post("/awards")
def create_award(
    name: str = Form(...),
    category: str = Form(...),
    criteria_id: str = Form(""),
    class_id: str = Form(""),
    db: Session = Depends(get_db),
):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    awards_service.create_award(
        db,
        show.id,
        name,
        AwardCategory(category),
        criteria_id=int(criteria_id) if criteria_id else None,
        class_id=int(class_id) if class_id else None,
    )
    return RedirectResponse("/awards", status_code=303)


@router.post("/awards/{award_id}/winner")
def set_winner(award_id: int, car_id: str = Form(""), db: Session = Depends(get_db)):
    awards_service.set_winner(db, award_id, int(car_id) if car_id else None)
    return RedirectResponse("/awards", status_code=303)


@router.post("/awards/{award_id}/delete")
def delete_award(award_id: int, db: Session = Depends(get_db)):
    awards_service.delete_award(db, award_id)
    return RedirectResponse("/awards", status_code=303)


@router.post("/awards/reorder")
async def reorder_awards(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return {"status": "error", "message": "No active show."}
    body = await request.json()
    order = [int(x) for x in body.get("order", [])]
    awards_service.reorder_awards(db, show.id, order)
    return {"status": "ok"}


@router.get("/awards/present")
def awards_present(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    slides = awards_service.build_presentation_sequence(db, show.id)
    return templates.TemplateResponse(
        request, "awards/present.html", {"request": request, "active_show": show, "slides": slides}
    )
