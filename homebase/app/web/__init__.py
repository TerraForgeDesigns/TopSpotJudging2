from fastapi import APIRouter

from app.web.awards import router as awards_router
from app.web.cars import router as cars_router
from app.web.classes import router as classes_router
from app.web.conflicts import router as conflicts_router
from app.web.criteria import router as criteria_router
from app.web.pages import router as pages_router
from app.web.photos import router as photos_router
from app.web.results import router as results_router
from app.web.shows import router as shows_router

router = APIRouter()
router.include_router(pages_router)
router.include_router(shows_router)
router.include_router(classes_router)
router.include_router(criteria_router)
router.include_router(cars_router)
router.include_router(conflicts_router)
router.include_router(photos_router)
router.include_router(results_router)
router.include_router(awards_router)

__all__ = ["router"]
