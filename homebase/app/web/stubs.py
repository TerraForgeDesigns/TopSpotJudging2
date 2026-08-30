"""
Placeholder routes for areas the Aug 2026 spec update reshaped but this
task (HB1 — foundation reconciliation) deliberately does not rebuild —
see DECISIONS.md. Each of these had a full implementation before this
task and will get a real one again in its own numbered prompt; until
then a plain "not built yet" page beats either a stale page built
against the old schema or a bare 404.
"""
from fastapi import APIRouter, Depends, Request
from sqlalchemy.orm import Session

from app.db import get_db
from app.templating import templates
from app.web.context import base_context

router = APIRouter()

_STUBS = {
    "/cars": (
        "Cars",
        "Setup",
        "Viewing and adding cars is being rebuilt around auto-generated entry "
        "numbers — see CONTEXT.md. Coming in HB3.",
    ),
    "/criteria": (
        "Judging Categories",
        "Setup",
        "The five built-in Judging Categories (Engine, Exterior, Interior, "
        "Paint, Wheels / Tires) are modeled, but the Show Setup screen that "
        "seeds and manages them is part of the Create Show wizard. Coming in HB2.",
    ),
    "/conflicts": (
        "Conflicts",
        "Judging",
        "Conflict resolution is being rebuilt for the new submission model. "
        "Flagged-conflict cars are still recorded correctly by sync; there's "
        "just no screen to resolve them yet.",
    ),
    "/results": (
        "Results",
        "Judging",
        "Results and rankings now use the new tie-break cascade and adjusted "
        "scores (see CONTEXT.md); the display, export, and print views that "
        "consume them haven't been rebuilt yet.",
    ),
    "/awards": (
        "Awards",
        "Awards",
        "Top Awards and nomination-based Show Awards (see CONTEXT.md) are "
        "modeled, but nomination-based winner resolution, Top Awards, Choose "
        "Winner, and the finish-show gate are coming in HB7.",
    ),
}


def _stub(path: str, title: str, eyebrow: str, message: str):
    @router.get(path, name=f"stub{path.replace('/', '_')}")
    def _view(request: Request, db: Session = Depends(get_db)):
        context = base_context(request, db, path)
        context.update(title=title, eyebrow=eyebrow, message=message)
        return templates.TemplateResponse(request, "coming_soon.html", context)

    return _view


for _path, (_title, _eyebrow, _message) in _STUBS.items():
    _stub(_path, _title, _eyebrow, _message)
