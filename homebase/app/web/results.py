import csv
import io

from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse, Response
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import criteria as criteria_service
from app.services import scoring
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


def _build_results_context(db: Session, show_id: int) -> dict:
    return {
        "overall": scoring.overall_rankings(db, show_id),
        "class_groups": scoring.class_rankings(db, show_id),
        "criteria_boards": scoring.criteria_leaderboards(db, show_id),
    }


@router.get("/results")
def results_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/results")
    context.update(_build_results_context(db, show.id))
    return templates.TemplateResponse(request, "results/index.html", context)


@router.get("/results/print")
def results_print(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = {"request": request, "active_show": show}
    context.update(_build_results_context(db, show.id))
    return templates.TemplateResponse(request, "results/print.html", context)


@router.get("/results/export.csv")
def results_export_csv(db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    all_criteria = criteria_service.list_criteria(db, show.id)
    overall = scoring.overall_rankings(db, show.id)

    buffer = io.StringIO()
    writer = csv.writer(buffer)
    header = ["rank", "tied", "registration_number", "car_number", "make", "model", "year", "class", "total_score"]
    header += [c.name for c in all_criteria]
    writer.writerow(header)

    for entry in overall:
        car = entry.car
        submission = scoring.get_accepted_submission(car)
        scores_by_id = {s.criteria_id: s.points for s in submission.scores} if submission else {}
        row = [
            entry.rank,
            "yes" if entry.tied else "no",
            car.registration_number,
            car.display_car_number,
            car.make,
            car.model,
            car.year,
            car.car_class.name if car.car_class else "Unclassified",
            entry.score,
        ]
        row += [scores_by_id.get(c.id, "") for c in all_criteria]
        writer.writerow(row)

    filename = f"{show.name.replace(' ', '_')}_results.csv"
    return Response(
        content=buffer.getvalue(),
        media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="{filename}"'},
    )
