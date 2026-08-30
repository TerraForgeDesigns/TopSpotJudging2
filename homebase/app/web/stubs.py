"""
Placeholder routes for Show Dashboard sections not yet rebuilt — see
DECISIONS.md. Cars, Judging (category setup), Awards (setup), Conflicts,
and Results are no longer here: Cars is a real page (web/cars.py);
Judging Setup and Awards management live under Edit Show (web/shows.py);
Conflicts (web/conflicts.py), Results (web/results.py), and Awards
winner resolution (web/awards.py) are real pages too — see the dashboard
tab strip in components/macros.html. Nothing is currently stubbed.
"""
from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()

_STUBS: dict = {}


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
