from fastapi import APIRouter, Request

from app.templating import templates
from app.web.nav import NAV_ITEMS

router = APIRouter()


def _base_context(request: Request, active_path: str) -> dict:
    return {
        "request": request,
        "nav_items": NAV_ITEMS,
        "active_path": active_path,
        # Placeholder connection status until the sync service exists — the
        # app shell always shows *something* here, never a blank area.
        "connection_status": "no_show_loaded",
    }


@router.get("/")
def dashboard(request: Request):
    context = _base_context(request, "/")
    return templates.TemplateResponse(request, "dashboard.html", context)


@router.get("/styleguide")
def styleguide(request: Request):
    context = _base_context(request, "/styleguide")
    return templates.TemplateResponse(request, "styleguide.html", context)
