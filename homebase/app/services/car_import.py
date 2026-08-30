"""
Bulk CSV roster import. Two-step, stateless: build_preview() validates
without writing anything, so the web layer can show every row (and any
row-level errors) before the host commits. commit_import() re-validates
from scratch against the CSV text alone — it never trusts a client-held
preview result, since the only thing carried between steps is the raw CSV
text itself (see /homebase/app/web/cars.py).
"""
import csv
import io
from dataclasses import dataclass, field

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.models import Car, CarClass
from app.services import sync as sync_service

REQUIRED_COLUMNS = ["registration_number", "car_number", "make", "model", "year", "class_name"]

MIN_YEAR = 1885  # the first automobile
MAX_YEAR = 2100


@dataclass
class ImportRow:
    row_number: int
    registration_number: str
    car_number: str
    make: str
    model: str
    year_raw: str
    class_name: str
    errors: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors


@dataclass
class ImportPreview:
    rows: list[ImportRow]
    header_error: str | None = None

    @property
    def valid_rows(self) -> list[ImportRow]:
        return [r for r in self.rows if r.ok]

    @property
    def error_rows(self) -> list[ImportRow]:
        return [r for r in self.rows if not r.ok]


@dataclass
class ImportSummary:
    imported_count: int
    skipped_rows: list[ImportRow]


def _read_rows(csv_text: str) -> tuple[list[dict], str | None]:
    reader = csv.DictReader(io.StringIO(csv_text))
    if reader.fieldnames is None:
        return [], "The file appears to be empty."
    missing = [c for c in REQUIRED_COLUMNS if c not in reader.fieldnames]
    if missing:
        return [], f"Missing required column(s): {', '.join(missing)}."
    return list(reader), None


def build_preview(db: Session, csv_text: str) -> ImportPreview:
    raw_rows, header_error = _read_rows(csv_text)
    if header_error:
        return ImportPreview(rows=[], header_error=header_error)

    # registration_number is globally unique (see models/car.py), not scoped
    # to a show, so check the whole table.
    existing_reg_numbers = {r for (r,) in db.execute(select(Car.registration_number))}

    rows: list[ImportRow] = []
    for i, raw in enumerate(raw_rows):
        row = ImportRow(
            row_number=i + 2,  # header is row 1
            registration_number=(raw.get("registration_number") or "").strip(),
            car_number=(raw.get("car_number") or "").strip(),
            make=(raw.get("make") or "").strip(),
            model=(raw.get("model") or "").strip(),
            year_raw=(raw.get("year") or "").strip(),
            class_name=(raw.get("class_name") or "").strip(),
        )

        if not row.registration_number:
            row.errors.append("Missing registration number.")
        if not row.car_number:
            row.errors.append("Missing car number.")
        if not row.make:
            row.errors.append("Missing make.")
        if not row.model:
            row.errors.append("Missing model.")
        if not row.year_raw:
            row.errors.append("Missing year.")
        else:
            try:
                year = int(row.year_raw)
                if not (MIN_YEAR <= year <= MAX_YEAR):
                    row.errors.append(f"Year '{row.year_raw}' is out of range.")
            except ValueError:
                row.errors.append(f"Year '{row.year_raw}' is not a number.")

        rows.append(row)

    _flag_duplicate_registration_numbers(rows, existing_reg_numbers)

    return ImportPreview(rows=rows)


def _flag_duplicate_registration_numbers(rows: list[ImportRow], existing_reg_numbers: set[str]) -> None:
    by_reg: dict[str, list[ImportRow]] = {}
    for row in rows:
        if row.registration_number:
            by_reg.setdefault(row.registration_number, []).append(row)

    for reg, group in by_reg.items():
        if reg in existing_reg_numbers:
            for row in group:
                row.errors.append(f"Registration number '{reg}' already exists in the roster.")
        if len(group) > 1:
            row_numbers = [r.row_number for r in group]
            for row in group:
                others = ", ".join(str(n) for n in row_numbers if n != row.row_number)
                row.errors.append(f"Duplicate registration number '{reg}' — also appears in row(s) {others}.")


def _get_or_create_class(db: Session, show_id: int, name: str, cache: dict[str, CarClass]) -> CarClass:
    key = name.strip().lower()
    if key in cache:
        return cache[key]

    existing = db.scalars(
        select(CarClass).where(CarClass.show_id == show_id, func.lower(CarClass.name) == key)
    ).first()
    if existing is not None:
        cache[key] = existing
        return existing

    max_order = db.scalar(
        select(CarClass.sort_order).where(CarClass.show_id == show_id).order_by(CarClass.sort_order.desc())
    )
    new_class = CarClass(show_id=show_id, name=name.strip(), sort_order=(max_order or 0) + 1)
    db.add(new_class)
    db.flush()  # need new_class.id before it's used on a Car below
    cache[key] = new_class
    return new_class


def commit_import(db: Session, show_id: int, csv_text: str) -> ImportSummary:
    """Re-validates csv_text from scratch (never trusts a prior preview) and
    imports only the rows that pass. Rows with errors are skipped, not
    rejected wholesale — see the preview shown to the host before this runs."""
    preview = build_preview(db, csv_text)

    class_cache: dict[str, CarClass] = {}
    created_cars: list[Car] = []
    for row in preview.valid_rows:
        class_id = None
        if row.class_name:
            car_class = _get_or_create_class(db, show_id, row.class_name, class_cache)
            class_id = car_class.id

        car = Car(
            show_id=show_id,
            registration_number=row.registration_number,
            display_car_number=row.car_number,
            make=row.make,
            model=row.model,
            year=int(row.year_raw),
            class_id=class_id,
        )
        db.add(car)
        created_cars.append(car)

    db.commit()

    # A late-registered car may have been judged (registration number
    # unmatched) before it existed here — see services/sync.py.
    for car in created_cars:
        sync_service.reconcile_unmatched_for_car(db, car)

    return ImportSummary(imported_count=len(created_cars), skipped_rows=preview.error_rows)
