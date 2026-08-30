from fastapi import APIRouter, Depends, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.orm import Session

from app.db import get_db
from app.services import sync as sync_service
from app.templating import templates
from app.web.context import base_context, require_active_show

router = APIRouter()


@router.get("/submissions/unmatched")
def unmatched_submissions(request: Request, db: Session = Depends(get_db)):
    show = require_active_show(db)
    if show is None:
        return RedirectResponse("/shows", status_code=303)

    context = base_context(request, db, "/submissions/unmatched")
    context["unmatched"] = sync_service.list_unmatched_submissions(db, show.id)
    return templates.TemplateResponse(request, "submissions/unmatched.html", context)
