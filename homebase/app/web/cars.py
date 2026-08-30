from fastapi import APIRouter, Depends, Form, Request, UploadFile
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import car_classes as car_classes_service
from app.services import car_import as car_import_service
from app.services import cars as cars_service
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/cars")
def list_cars(
    request: Request,
    imported: int | None = None,
    skipped: int | None = None,
    duplicate: str | None = None,
    db: Session = Depends(get_db),
):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/cars")
    context["cars"] = cars_service.list_cars(db, show.id)
    context["classes"] = [c for c, _ in car_classes_service.list_classes_with_counts(db, show.id)]
    context["import_result"] = (
        {"imported": imported, "skipped": skipped} if imported is not None or skipped is not None else None
    )
    context["duplicate"] = duplicate
    return templates.TemplateResponse(request, "cars/list.html", context)


@router.post("/cars")
def create_car(
    registration_number: str = Form(...),
    display_car_number: str = Form(...),
    make: str = Form(...),
    model: str = Form(...),
    year: int = Form(...),
    class_id: str = Form(""),
    db: Session = Depends(get_db),
):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    resolved_class_id = int(class_id) if class_id else None
    try:
        cars_service.create_car(
            db, show.id, registration_number, display_car_number, make, model, year, resolved_class_id
        )
    except cars_service.DuplicateRegistrationNumber:
        # Roster is the primary key humans work from all day (CONTEXT.md) — a
        # silently-dropped duplicate would be worse than just not saving it.
        # Send the host back with the number so they can see what happened.
        return RedirectResponse(f"/cars?duplicate={registration_number}", status_code=303)

    return RedirectResponse("/cars", status_code=303)


@router.get("/cars/{car_id}/edit")
def edit_car(car_id: int, request: Request, duplicate: str | None = None, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    car = cars_service.get_car(db, car_id)
    if car is None:
        return RedirectResponse("/cars", status_code=303)

    context = base_context(request, db, "/cars")
    context["car"] = car
    context["classes"] = [c for c, _ in car_classes_service.list_classes_with_counts(db, show.id)]
    context["judging_data"] = cars_service.judging_data_summary(db, car_id)
    context["duplicate"] = duplicate
    return templates.TemplateResponse(request, "cars/edit.html", context)


@router.post("/cars/{car_id}")
def update_car(
    car_id: int,
    registration_number: str = Form(...),
    display_car_number: str = Form(...),
    make: str = Form(...),
    model: str = Form(...),
    year: int = Form(...),
    class_id: str = Form(""),
    owner_name: str = Form(""),
    announcer_name: str = Form(""),
    db: Session = Depends(get_db),
):
    resolved_class_id = int(class_id) if class_id else None
    try:
        cars_service.update_car(
            db,
            car_id,
            registration_number,
            display_car_number,
            make,
            model,
            year,
            resolved_class_id,
            owner_name,
            announcer_name,
        )
    except cars_service.DuplicateRegistrationNumber:
        return RedirectResponse(f"/cars/{car_id}/edit?duplicate={registration_number}", status_code=303)

    return RedirectResponse("/cars", status_code=303)


@router.post("/cars/{car_id}/delete")
def delete_car(car_id: int, db: Session = Depends(get_db)):
    cars_service.delete_car(db, car_id)
    return RedirectResponse("/cars", status_code=303)


@router.get("/cars/import")
def import_cars_form(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)
    context = base_context(request, db, "/cars")
    context["preview"] = None
    return templates.TemplateResponse(request, "cars/import.html", context)


@router.post("/cars/import/preview")
async def import_cars_preview(request: Request, file: UploadFile, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    raw = await file.read()
    csv_text = raw.decode("utf-8-sig", errors="replace")

    preview = car_import_service.build_preview(db, csv_text)

    context = base_context(request, db, "/cars")
    context["preview"] = preview
    context["csv_text"] = csv_text
    return templates.TemplateResponse(request, "cars/import.html", context)


@router.post("/cars/import/confirm")
def import_cars_confirm(csv_text: str = Form(...), db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    summary = car_import_service.commit_import(db, show.id, csv_text)
    return RedirectResponse(
        f"/cars?imported={summary.imported_count}&skipped={len(summary.skipped_rows)}", status_code=303
    )
