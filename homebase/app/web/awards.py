"""
Award winner resolution + Finish Show — see CONTEXT.md's Awards section
and this task's Finishing the Show requirement. Distinct from
web/shows.py's award SLOT setup (add/rename/reorder/toggle) — this page
is entirely about deciding and recording who won, and, once every award
is resolved, finishing the show.
"""
from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import PhotoType, Show
from app.services.award_results import (
    award_rankings,
    choose_winner,
    high_scorers_not_nominated,
    mark_not_presented,
    nominations_per_judge_report,
    set_announcer_name,
    suggest_award_winner,
    use_suggested_winner,
)
from app.services.awards_setup import RANKING_BASIS_LABELS, get_award, list_awards
from app.services.cars import get_car, list_cars
from app.services.finish_show import can_finish_show, finish_show, outstanding_awards
from app.services.photo_ingest import get_car_photo
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


def _award_view(db: Session, show: Show, award):
    view = {
        "award": award,
        "basis_label": RANKING_BASIS_LABELS.get(award.ranking_basis) if award.ranking_basis else None,
        "nominees": [],
        "suggestion": None,
        "high_scorers": [],
    }
    if award.judge_chosen:
        view["nominees"] = award_rankings(db, show, award)
        view["suggestion"] = suggest_award_winner(db, show, award)
        view["high_scorers"] = high_scorers_not_nominated(db, show, award)
    return view


def _awards_page_context(request: Request, db: Session, show: Show) -> dict:
    awards = [a for a in list_awards(db, show.id) if a.active]
    winner_photos = {}
    for award in awards:
        if award.winner_car_id:
            photo = get_car_photo(db, award.winner_car_id, PhotoType.JUDGE_SHEET)
            winner_photos[award.id] = photo

    context = base_context(request, db, "/awards")
    context.update(
        award_views=[_award_view(db, show, a) for a in awards],
        winner_photos=winner_photos,
        cars=[row.car for row in list_cars(db, show.id)],
        nomination_report=nominations_per_judge_report(db, show.id),
        outstanding=outstanding_awards(db, show),
        can_finish=can_finish_show(db, show),
    )
    return context


@router.get("/awards")
def awards_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    return templates.TemplateResponse(request, "awards/index.html", _awards_page_context(request, db, show))


@router.post("/awards/{award_id}/choose-winner")
def choose_winner_route(award_id: int, car_id: str = Form(""), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    award = get_award(db, award_id)
    choose_winner(db, show, award, int(car_id) if car_id else None)
    return RedirectResponse("/awards", status_code=303)


@router.post("/awards/{award_id}/use-suggested")
def use_suggested_route(award_id: int, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    award = get_award(db, award_id)
    use_suggested_winner(db, show, award)
    return RedirectResponse("/awards", status_code=303)


@router.post("/awards/{award_id}/not-presented")
def not_presented_route(award_id: int, flag: str = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    award = get_award(db, award_id)
    mark_not_presented(db, show, award, flag == "yes")
    return RedirectResponse("/awards", status_code=303)


@router.post("/awards/announcer-name")
def announcer_name_route(car_id: int = Form(...), name: str = Form(""), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = get_car(db, car_id)
    if car is not None and car.show_id == show.id:
        set_announcer_name(db, show, car, name)
    return RedirectResponse("/awards", status_code=303)


@router.post("/shows/{show_id}/finish")
def finish_show_route(show_id: int, db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    finish_show(db, show)
    return RedirectResponse("/awards", status_code=303)
