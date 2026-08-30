"""
Results — full rankings, per-category leaderboards, Top Awards (with its
boundary-tie resolution), CSV export, and the printable view. See
CONTEXT.md's Results requirements and services/results.py.
"""
import csv
import io

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse, Response
from sqlalchemy.orm import Session

from app.db import get_db
from app.services.results import build_results_export, get_top_awards, resolve_top_awards
from app.services.scoring import category_leaderboards, overall_rankings
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


def _results_context(request: Request, db: Session, show) -> dict:
    context = base_context(request, db, "/results")
    context["ranked"] = overall_rankings(db, show.id)
    context["leaderboards"] = category_leaderboards(db, show.id)
    context["top_awards"] = get_top_awards(db, show)
    return context


@router.get("/results")
def results_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    return templates.TemplateResponse(request, "results/index.html", _results_context(request, db, show))


@router.post("/results/top-awards/resolve")
def resolve_top_awards_boundary(car_ids: list[int] = Form(default=[]), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    resolve_top_awards(db, show, set(car_ids))
    return RedirectResponse("/results", status_code=303)


@router.get("/results/print")
def results_print(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/results")
    context["show"] = show
    context["rows"] = build_results_export(db, show)
    context["leaderboards"] = category_leaderboards(db, show.id)
    return templates.TemplateResponse(request, "results/print.html", context)


@router.get("/results/export.csv")
def results_export_csv(db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    rows = build_results_export(db, show)
    category_names = list(rows[0].category_scores.keys()) if rows else []

    buffer = io.StringIO()
    writer = csv.writer(buffer)
    writer.writerow(["Rank", "Tied", "Entry #", "Participant", "Year", "Make", "Model", "Total"] + category_names)
    for row in rows:
        writer.writerow(
            [row.rank, "Yes" if row.tied else "No", row.entry_number, row.participant, row.year, row.make, row.model, row.total]
            + [row.category_scores.get(name, "") for name in category_names]
        )

    filename = f"{show.name.replace(' ', '_')}_results.csv"
    return Response(
        content=buffer.getvalue(),
        media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="{filename}"'},
    )
