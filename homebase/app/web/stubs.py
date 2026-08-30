"""
Placeholder routes for Show Dashboard sections not yet rebuilt — see
DECISIONS.md. Cars, Judging (category setup), and Awards (setup) are no
longer here: Cars is a real page (web/cars.py); Judging Setup and Awards
management now live under Edit Show (web/shows.py) — see the dashboard
tab strip in components/macros.html. What's left is genuinely
unbuilt: conflict resolution and results/rankings display.
"""
from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()

_STUBS = {
    "/conflicts": (
        "Conflicts",
        "Judging",
        "judging",
        "Conflict resolution is being rebuilt for the new submission model. "
        "Flagged-conflict cars are still recorded correctly when two handhelds "
        "score the same entry; there's just no screen to resolve them yet.",
    ),
    "/results": (
        "Results",
        "Judging",
        "results",
        "Results and rankings now use the new tie-break cascade and adjusted "
        "scores (see CONTEXT.md); the display, export, and print views that "
        "consume them haven't been rebuilt yet.",
    ),
}


def _stub(path: str, title: str, eyebrow: str, tab: str, message: str):
    @router.get(path, name=f"stub{path.replace('/', '_')}")
    def _view(request: Request, db: Session = Depends(get_db)):
        show = require_active_show(db)
        if show is None:
            return RedirectResponse("/shows", status_code=303)
        context = base_context(request, db, path)
        context.update(title=title, eyebrow=eyebrow, tab=tab, message=message)
        return templates.TemplateResponse(request, "coming_soon.html", context)

    return _view


for _path, (_title, _eyebrow, _tab, _message) in _STUBS.items():
    _stub(_path, _title, _eyebrow, _tab, _message)
