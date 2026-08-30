"""
Shows list/switch, and the Edit Show page — see CONTEXT.md's Edit Show
section: "grouped by what the organiser is trying to do, not one
settings screen." Four groups, each its own card on one page: Show
Details (plain form), Add Cars (its own form + a plain-language
confirmation rendered back on success), Judging Setup and Awards (both
"live" — small htmx actions that persist immediately and re-render their
own fragment, same pattern as the Create Show wizard's Steps 3/4).

Every protection rule this page might hit lives in services/
(judging_category_rules.py via judging_categories.py) — this module
never contains a conditional deciding whether a change is allowed, only
whether to display the reason a service function already returned.
"""
from datetime import date

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import Show
from app.models.enums import AwardRankingBasis
from app.services import awards_setup, cars as cars_service, judging_categories, shows as shows_service
from app.templating import templates
from app.web.context import base_context

router = APIRouter()


@router.get("/shows")
def list_shows(request: Request, db: Session = Depends(get_db)):
    context = base_context(request, db, "/shows")
    context["shows"] = shows_service.list_shows(db)
    return templates.TemplateResponse(request, "shows/list.html", context)


def _edit_show_context(request: Request, db: Session, show: Show) -> dict:
    context = base_context(request, db, "/shows")
    context.update(
        show=show,
        categories=judging_categories.list_categories(db, show.id),
        max_score=sum(1 for c in judging_categories.list_categories(db, show.id) if c.active) * show.score_range_max,
        awards=awards_setup.list_awards(db, show.id),
        ranking_basis_options=awards_setup.RANKING_BASIS_OPTIONS,
        ranking_basis_labels=awards_setup.RANKING_BASIS_LABELS,
        add_cars_result=None,
        add_cars_error=None,
        category_error=None,
        add_award_errors={},
    )
    return context


@router.get("/shows/{show_id}/edit")
def edit_show(show_id: int, request: Request, db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    if show is None:
        return RedirectResponse("/shows", status_code=303)
    context = _edit_show_context(request, db, show)
    return templates.TemplateResponse(request, "shows/edit.html", context)


# ---------------------------------------------------------------------
# Show Details — always editable
# ---------------------------------------------------------------------

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


# ---------------------------------------------------------------------
# Add Cars
# ---------------------------------------------------------------------

@router.post("/shows/{show_id}/add-cars")
def add_cars_submit(show_id: int, request: Request, count: str = Form(""), db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = _edit_show_context(request, db, show)
    count = (count or "").strip()
    if not count.isdigit() or int(count) <= 0:
        context["add_cars_error"] = "How many cars do you want to add? Enter a number greater than zero."
        return templates.TemplateResponse(request, "shows/edit.html", context)

    result = cars_service.add_cars(db, show, int(count))
    # _edit_show_context was built before add_cars ran — rebuild so Max
    # Score and the escalated range reflect what just happened.
    context = _edit_show_context(request, db, show)
    context["add_cars_result"] = result
    return templates.TemplateResponse(request, "shows/edit.html", context)


# ---------------------------------------------------------------------
# Judging Setup — editable, subject to the protection rules
# ---------------------------------------------------------------------

def _judging_fragment_context(db: Session, show: Show, error: str | None = None) -> dict:
    categories = judging_categories.list_categories(db, show.id)
    return {
        "show": show,
        "categories": categories,
        "max_score": sum(1 for c in categories if c.active) * show.score_range_max,
        "category_error": error,
    }


@router.post("/shows/{show_id}/categories/{category_id}/toggle")
def toggle_category(show_id: int, category_id: int, request: Request, db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    category = judging_categories.get_category(db, category_id)
    error = judging_categories.toggle_category(db, show, category)
    return templates.TemplateResponse(
        request, "partials/edit_show_judging.html", _judging_fragment_context(db, show, error)
    )


@router.post("/shows/{show_id}/categories/{category_id}/rename")
def rename_category(show_id: int, category_id: int, request: Request, name: str = Form(""), db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    category = judging_categories.get_category(db, category_id)
    judging_categories.rename_category(db, show, category, name)
    return templates.TemplateResponse(request, "partials/edit_show_judging.html", _judging_fragment_context(db, show))


@router.post("/shows/{show_id}/categories/reorder")
def reorder_categories(show_id: int, request: Request, order: str = Form(""), db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    ordered_ids = [int(x) for x in order.split(",") if x]
    judging_categories.reorder_categories(db, show, ordered_ids)
    return templates.TemplateResponse(request, "partials/edit_show_judging.html", _judging_fragment_context(db, show))


@router.post("/shows/{show_id}/overall-impression/toggle")
def toggle_overall_impression(show_id: int, request: Request, db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    judging_categories.toggle_overall_impression(db, show)
    return templates.TemplateResponse(request, "partials/edit_show_judging.html", _judging_fragment_context(db, show))


# ---------------------------------------------------------------------
# Awards — always allowed, including while the show is running
# ---------------------------------------------------------------------

def _awards_fragment_context(db: Session, show: Show, add_errors: dict | None = None) -> dict:
    return {
        "show": show,
        "awards": awards_setup.list_awards(db, show.id),
        "ranking_basis_options": awards_setup.RANKING_BASIS_OPTIONS,
        "ranking_basis_labels": awards_setup.RANKING_BASIS_LABELS,
        "add_award_errors": add_errors or {},
    }


def _parse_ranking_basis(raw: str) -> AwardRankingBasis | None:
    try:
        return AwardRankingBasis(raw) if raw else None
    except ValueError:
        return None


@router.post("/shows/{show_id}/awards/add")
def add_award(
    show_id: int,
    request: Request,
    name: str = Form(""),
    judge_chosen: str = Form(""),
    ranking_basis: str = Form(""),
    db: Session = Depends(get_db),
):
    show = db.get(Show, show_id)
    result = awards_setup.add_award(db, show, name, judge_chosen == "yes", _parse_ranking_basis(ranking_basis))
    return templates.TemplateResponse(
        request, "partials/edit_show_awards.html", _awards_fragment_context(db, show, result.errors)
    )


@router.post("/shows/{show_id}/awards/{award_id}/rename")
def rename_award(show_id: int, award_id: int, request: Request, name: str = Form(""), db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    award = awards_setup.get_award(db, award_id)
    awards_setup.rename_award(db, show, award, name)
    return templates.TemplateResponse(request, "partials/edit_show_awards.html", _awards_fragment_context(db, show))


@router.post("/shows/{show_id}/awards/{award_id}/toggle")
def toggle_award(show_id: int, award_id: int, request: Request, db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    award = awards_setup.get_award(db, award_id)
    awards_setup.toggle_award(db, show, award)
    return templates.TemplateResponse(request, "partials/edit_show_awards.html", _awards_fragment_context(db, show))


@router.post("/shows/{show_id}/awards/reorder")
def reorder_awards(show_id: int, request: Request, order: str = Form(""), db: Session = Depends(get_db)):
    show = db.get(Show, show_id)
    ordered_ids = [int(x) for x in order.split(",") if x]
    awards_setup.reorder_awards(db, show, ordered_ids)
    return templates.TemplateResponse(request, "partials/edit_show_awards.html", _awards_fragment_context(db, show))


@router.post("/shows/{show_id}/awards/{award_id}/winner-choice")
def set_winner_choice(
    show_id: int,
    award_id: int,
    request: Request,
    judge_chosen: str = Form(""),
    ranking_basis: str = Form(""),
    db: Session = Depends(get_db),
):
    show = db.get(Show, show_id)
    award = awards_setup.get_award(db, award_id)
    awards_setup.set_winner_choice(db, show, award, judge_chosen == "yes", _parse_ranking_basis(ranking_basis))
    return templates.TemplateResponse(request, "partials/edit_show_awards.html", _awards_fragment_context(db, show))
