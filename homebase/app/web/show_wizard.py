"""
Routes for the Create Show wizard — see CONTEXT.md's "Show setup"
section and services/show_wizard.py for the business logic. This module
is a thin wrapper: parse the request, call a service function, render a
template or redirect.

Every step's fields are saved server-side against the ShowDraft the
moment they're submitted (see services/show_wizard.py's module
docstring) — nothing about the wizard's business data ever rides along
as a hidden form field. The `from=review` query param that shows up on
several routes is different: it's pure navigation context (does
Continue go to the next step, or back to Review?), not wizard state.
"""
from fastapi import APIRouter, Depends, Form, Query, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.models import ShowDraft
from app.services import show_wizard
from app.templating import templates
from app.web.context import base_context

router = APIRouter()


def _draft_or_404_redirect(db: Session, draft_id: int) -> ShowDraft | None:
    return show_wizard.get_draft(db, draft_id)


def _guard_step_order(data: dict, requested_step: int) -> int | None:
    """Returns the step to redirect to if `requested_step` isn't safely
    reachable yet, else None. Steps 3/4 always have valid defaults (all
    categories active, no awards required), so only 1 and 2 gate
    anything — see services/show_wizard.py."""
    if requested_step >= 2 and not (data.get("name") and data.get("event_date")):
        return 1
    if requested_step >= 3 and not data.get("car_count"):
        return 2
    return None


def _step_url(draft_id: int, step: int, from_review: bool = False) -> str:
    suffix = "?from=review" if from_review else ""
    return f"/shows/new/{draft_id}/step/{step}{suffix}"


def _continue_target(draft_id: int, next_step: int, from_review: str | None) -> str:
    if from_review:
        return _step_url(draft_id, 5)
    return _step_url(draft_id, next_step)


@router.get("/shows/new")
def start_wizard(db: Session = Depends(get_db)):
    draft = show_wizard.create_draft(db)
    return RedirectResponse(_step_url(draft.id, 1), status_code=303)


# ---------------------------------------------------------------------
# Step 1 — Show Details
# ---------------------------------------------------------------------

@router.get("/shows/new/{draft_id}/step/1")
def step1_form(draft_id: int, request: Request, from_review: str | None = None, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)
    context = base_context(request, db, "/shows")
    context.update(draft=draft, errors={}, values=draft.data, from_review=bool(from_review))
    return templates.TemplateResponse(request, "wizard/step1.html", context)


@router.post("/shows/new/{draft_id}/step/1")
def step1_submit(
    draft_id: int,
    request: Request,
    name: str = Form(""),
    event_date: str = Form(""),
    location: str = Form(""),
    notes: str = Form(""),
    from_review: str | None = Query(None, alias="from"),
    db: Session = Depends(get_db),
):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)

    errors = show_wizard.save_step1(db, draft, name, event_date, location, notes)
    if errors:
        context = base_context(request, db, "/shows")
        context.update(
            draft=draft,
            errors=errors,
            values={"name": name, "event_date": event_date, "location": location, "notes": notes},
            from_review=bool(from_review),
        )
        return templates.TemplateResponse(request, "wizard/step1.html", context)

    return RedirectResponse(_continue_target(draft_id, 2, from_review), status_code=303)


# ---------------------------------------------------------------------
# Step 2 — Number of Cars
# ---------------------------------------------------------------------

@router.get("/shows/new/{draft_id}/step/2")
def step2_form(draft_id: int, request: Request, from_review: str | None = None, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)
    redirect_step = _guard_step_order(draft.data, 2)
    if redirect_step:
        return RedirectResponse(_step_url(draft_id, redirect_step), status_code=303)

    context = base_context(request, db, "/shows")
    car_count = draft.data.get("car_count")
    context.update(
        draft=draft,
        errors={},
        car_count=car_count,
        has_count=car_count is not None,
        range_max=show_wizard.range_max_for_draft(draft.data),
        nudge=show_wizard.near_threshold_nudge(car_count),
        from_review=bool(from_review),
    )
    return templates.TemplateResponse(request, "wizard/step2.html", context)


@router.post("/shows/new/{draft_id}/step/2")
def step2_submit(
    draft_id: int,
    request: Request,
    car_count: str = Form(""),
    from_review: str | None = Query(None, alias="from"),
    db: Session = Depends(get_db),
):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)

    errors = show_wizard.save_step2(db, draft, car_count)
    if errors:
        context = base_context(request, db, "/shows")
        parsed = int(car_count) if car_count.strip().isdigit() else None
        context.update(
            draft=draft,
            errors=errors,
            car_count=car_count,
            has_count=parsed is not None,
            range_max=show_wizard.range_max_for_draft({**draft.data, "car_count": parsed}),
            nudge=show_wizard.near_threshold_nudge(parsed),
            from_review=bool(from_review),
        )
        return templates.TemplateResponse(request, "wizard/step2.html", context)

    return RedirectResponse(_continue_target(draft_id, 3, from_review), status_code=303)


@router.get("/shows/new/{draft_id}/step/2/preview")
def step2_preview(draft_id: int, request: Request, car_count: str = "", db: Session = Depends(get_db)):
    """htmx partial: live score-range info + near-threshold nudge as the
    host types, before Continue is even clicked — no draft write."""
    parsed = int(car_count) if car_count.strip().isdigit() and int(car_count) > 0 else None
    context = {
        "request": request,
        "range_max": show_wizard.range_max_for_draft({"car_count": parsed}),
        "nudge": show_wizard.near_threshold_nudge(parsed),
        "has_count": parsed is not None,
    }
    return templates.TemplateResponse(request, "wizard/_step2_preview.html", context)


# ---------------------------------------------------------------------
# Step 3 — Judging Setup
# ---------------------------------------------------------------------

def _step3_live_context(request: Request, db: Session, draft: ShowDraft) -> dict:
    return {
        "request": request,
        "draft": draft,
        "categories": sorted(draft.data["categories"], key=lambda c: c["sort_order"]),
        "active_categories": show_wizard.active_categories_in_priority_order(draft.data),
        "max_score": show_wizard.max_score(draft.data),
        "range_max": show_wizard.range_max_for_draft(draft.data),
        "overall_impression_enabled": draft.data["overall_impression_enabled"],
    }


@router.get("/shows/new/{draft_id}/step/3")
def step3_form(draft_id: int, request: Request, from_review: str | None = None, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)
    redirect_step = _guard_step_order(draft.data, 3)
    if redirect_step:
        return RedirectResponse(_step_url(draft_id, redirect_step), status_code=303)

    context = base_context(request, db, "/shows")
    context.update(_step3_live_context(request, db, draft))
    context.update(error=None, from_review=bool(from_review))
    return templates.TemplateResponse(request, "wizard/step3.html", context)


@router.post("/shows/new/{draft_id}/step/3/category/{category_id}/toggle")
def step3_toggle_category(draft_id: int, category_id: str, request: Request, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.toggle_category(db, draft, category_id)
    return templates.TemplateResponse(request, "wizard/_step3_live.html", _step3_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/step/3/category/{category_id}/rename")
def step3_rename_category(
    draft_id: int, category_id: str, request: Request, name: str = Form(""), db: Session = Depends(get_db)
):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.rename_category(db, draft, category_id, name)
    return templates.TemplateResponse(request, "wizard/_step3_live.html", _step3_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/step/3/priority-order")
def step3_priority_order(draft_id: int, request: Request, order: str = Form(""), db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    ordered_ids = [x for x in order.split(",") if x]
    show_wizard.reorder_categories(db, draft, ordered_ids)
    return templates.TemplateResponse(request, "wizard/_step3_live.html", _step3_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/step/3/overall-impression")
def step3_overall_impression(draft_id: int, request: Request, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.toggle_overall_impression(db, draft)
    return templates.TemplateResponse(request, "wizard/_step3_live.html", _step3_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/step/3/continue")
def step3_continue(
    draft_id: int, request: Request, from_review: str | None = Query(None, alias="from"), db: Session = Depends(get_db)
):
    draft = _draft_or_404_redirect(db, draft_id)
    error = show_wizard.validate_step3(draft.data)
    if error:
        context = base_context(request, db, "/shows")
        context.update(_step3_live_context(request, db, draft))
        context.update(error=error, from_review=bool(from_review))
        return templates.TemplateResponse(request, "wizard/step3.html", context)

    return RedirectResponse(_continue_target(draft_id, 4, from_review), status_code=303)


# ---------------------------------------------------------------------
# Step 4 — Awards
# ---------------------------------------------------------------------

def _step4_live_context(request: Request, db: Session, draft: ShowDraft, add_errors: dict | None = None) -> dict:
    return {
        "request": request,
        "draft": draft,
        "top_award_options": show_wizard.TOP_AWARD_OPTIONS,
        "top_awards_count": draft.data.get("top_awards_count"),
        "awards": sorted(draft.data["awards"], key=lambda a: a["sort_order"]),
        "ranking_basis_options": show_wizard.RANKING_BASIS_OPTIONS,
        "ranking_basis_labels": show_wizard.RANKING_BASIS_LABELS,
        "add_errors": add_errors or {},
    }


@router.get("/shows/new/{draft_id}/step/4")
def step4_form(draft_id: int, request: Request, from_review: str | None = None, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)
    redirect_step = _guard_step_order(draft.data, 4)
    if redirect_step:
        return RedirectResponse(_step_url(draft_id, redirect_step), status_code=303)

    context = base_context(request, db, "/shows")
    context.update(_step4_live_context(request, db, draft))
    context.update(error=None, from_review=bool(from_review))
    return templates.TemplateResponse(request, "wizard/step4.html", context)


@router.post("/shows/new/{draft_id}/step/4/top-awards")
def step4_top_awards(draft_id: int, request: Request, count: int = Form(...), db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.set_top_awards_count(db, draft, count)
    return templates.TemplateResponse(request, "wizard/_step4_live.html", _step4_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/award/{award_id}/toggle")
def step4_toggle_award(draft_id: int, award_id: int, request: Request, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.toggle_award(db, draft, award_id)
    return templates.TemplateResponse(request, "wizard/_step4_live.html", _step4_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/award/{award_id}/rename")
def step4_rename_award(
    draft_id: int, award_id: int, request: Request, name: str = Form(""), db: Session = Depends(get_db)
):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.rename_award(db, draft, award_id, name)
    return templates.TemplateResponse(request, "wizard/_step4_live.html", _step4_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/award/{award_id}/remove")
def step4_remove_award(draft_id: int, award_id: int, request: Request, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    show_wizard.remove_award(db, draft, award_id)
    return templates.TemplateResponse(request, "wizard/_step4_live.html", _step4_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/award/reorder")
def step4_reorder_awards(draft_id: int, request: Request, order: str = Form(""), db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    ordered_ids = [int(x) for x in order.split(",") if x]
    show_wizard.reorder_awards(db, draft, ordered_ids)
    return templates.TemplateResponse(request, "wizard/_step4_live.html", _step4_live_context(request, db, draft))


@router.post("/shows/new/{draft_id}/award/add")
def step4_add_award(
    draft_id: int,
    request: Request,
    name: str = Form(""),
    judge_chosen: str = Form(""),
    ranking_basis: str = Form(""),
    db: Session = Depends(get_db),
):
    draft = _draft_or_404_redirect(db, draft_id)
    result = show_wizard.add_award(db, draft, name, judge_chosen == "yes", ranking_basis or None)
    return templates.TemplateResponse(
        request, "wizard/_step4_live.html", _step4_live_context(request, db, draft, result.errors)
    )


@router.post("/shows/new/{draft_id}/step/4/continue")
def step4_continue(
    draft_id: int, request: Request, from_review: str | None = Query(None, alias="from"), db: Session = Depends(get_db)
):
    draft = _draft_or_404_redirect(db, draft_id)
    error = show_wizard.validate_step4(draft.data)
    if error:
        context = base_context(request, db, "/shows")
        context.update(_step4_live_context(request, db, draft))
        context.update(error=error, from_review=bool(from_review))
        return templates.TemplateResponse(request, "wizard/step4.html", context)

    return RedirectResponse(_continue_target(draft_id, 5, from_review), status_code=303)


# ---------------------------------------------------------------------
# Step 5 — Review, and Step 6 — Create Show
# ---------------------------------------------------------------------

@router.get("/shows/new/{draft_id}/step/5")
def step5_review(draft_id: int, request: Request, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)
    redirect_step = _guard_step_order(draft.data, 5)
    if redirect_step:
        return RedirectResponse(_step_url(draft_id, redirect_step), status_code=303)

    data = draft.data
    car_count = data["car_count"]
    context = base_context(request, db, "/shows")
    context.update(
        draft=draft,
        entry_range=f"001 through {car_count:03d}",
        max_score=show_wizard.max_score(data),
        range_max=show_wizard.range_max_for_draft(data),
        active_categories=show_wizard.active_categories_in_priority_order(data),
        active_awards=show_wizard.active_awards_in_order(data),
        ranking_basis_labels=show_wizard.RANKING_BASIS_LABELS,
    )
    return templates.TemplateResponse(request, "wizard/step5.html", context)


@router.post("/shows/new/{draft_id}/create")
def create_show(draft_id: int, db: Session = Depends(get_db)):
    draft = _draft_or_404_redirect(db, draft_id)
    if draft is None:
        return RedirectResponse("/shows/new", status_code=303)
    show_wizard.materialize(db, draft)
    return RedirectResponse("/", status_code=303)
