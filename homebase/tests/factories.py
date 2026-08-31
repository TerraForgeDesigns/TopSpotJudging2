"""
Show-construction helpers for INT1's integration tests — not a test file
itself (no test_ functions), just shared setup so a 310-car show doesn't
need 310 lines of fixture code in every test that needs one.

build_show() drives the REAL show_wizard service functions
(create_draft/save_step1/save_step2/materialize — the same path
services/show_wizard.py's own web routes use), not a hand-rolled
Show/JudgingCategory/Award insert — so category/award defaults (all 5
Judging Categories active, all 7 built-in Show Awards active and
judge-chosen — CONTEXT.md's Awards table) and the initial score range
(range_max_for_car_count(car_count), same tiers CONTEXT.md documents)
come from production code, not reimplemented here.
"""
from datetime import date

from sqlalchemy.orm import Session

from app.models import Show
from app.services import show_wizard


def build_show(
    db: Session,
    *,
    car_count: int = 1,
    overall_impression_enabled: bool = False,
    top_awards_count: int | None = None,
    name: str = "Integration Test Show",
    event_date: date | None = None,
) -> Show:
    draft = show_wizard.create_draft(db)
    event_date = event_date or date.today()

    errors = show_wizard.save_step1(db, draft, name, event_date.isoformat(), "Test Field", "")
    assert not errors, errors

    errors = show_wizard.save_step2(db, draft, str(car_count))
    assert not errors, errors

    if overall_impression_enabled:
        show_wizard.toggle_overall_impression(db, draft)
    if top_awards_count is not None:
        show_wizard.set_top_awards_count(db, draft, top_awards_count)

    return show_wizard.materialize(db, draft)
