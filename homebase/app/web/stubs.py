"""
Placeholder routes for Show Dashboard sections the Aug 2026 spec update
reshaped but this task doesn't rebuild — see DECISIONS.md. Each had a
full implementation before the HB1 reconciliation and will get a real
one again in its own numbered prompt; until then a plain "not built
yet" page beats either a stale page built against the deleted schema
or a bare 404. All four require an active show, like every other Show
Dashboard section.
"""
from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()

_STUBS = {
    "/cars": (
        "Cars",
        "Setup",
        "cars",
        "Viewing and adding cars is being rebuilt around auto-generated entry "
        "numbers — see CONTEXT.md. Entries already exist for this show (created "
        "by the Create Show wizard); there's just no screen to view or fill them "
        "in yet.",
    ),
    "/criteria": (
        "Judging",
        "Setup",
        "judging",
        "The five built-in Judging Categories were set up for this show in the "
        "Create Show wizard. There's no screen yet to change that setup — rename, "
        "toggle, or reorder — after a show is created.",
    ),
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
    "/awards": (
        "Awards",
        "Awards",
        "awards",
        "Top Awards and Show Awards were set up for this show in the Create Show "
        "wizard. Nomination-based winner resolution, Top Awards computation, "
        "Choose Winner, and the finish-show gate are coming in a later prompt.",
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
