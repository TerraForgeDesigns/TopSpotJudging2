"""
The handheld simulator (SIM1) — a browser reproduction of the ESP32
handheld's actual screens/theme/interaction model, served same-origin so
it can call the real POST /api/v1/sync (and, eventually,
/api/v1/photos/upload) directly with zero CORS setup. See
static/simulator/js/*.js for the actual behavior — this route only
serves the shell page; everything else lives client-side, same as any
other single-page tool this project vendors rather than proxies through
htmx (the simulator's screen model doesn't fit htmx's server-round-trip
shape at all — it's replicating an offline-first embedded UI, not a
web page).

Requires an active show, same as every other page that operates on show
data (CONTEXT.md) — a sync round trip against no show is meaningless.
"""
from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.templating import templates
from app.web.context import require_active_show

router = APIRouter()


@router.get("/simulator")
def simulator_home(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    return templates.TemplateResponse(request, "simulator/index.html", {"request": request, "show_name": show.name})
