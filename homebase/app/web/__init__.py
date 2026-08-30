from fastapi import APIRouter

from app.web.cars import router as cars_router
from app.web.classes import router as classes_router
from app.web.criteria import router as criteria_router
from app.web.pages import router as pages_router
from app.web.shows import router as shows_router
from app.web.submissions import router as submissions_router

router = APIRouter()
router.include_router(pages_router)
router.include_router(shows_router)
router.include_router(classes_router)
router.include_router(criteria_router)
router.include_router(cars_router)
router.include_router(submissions_router)

__all__ = ["router"]
