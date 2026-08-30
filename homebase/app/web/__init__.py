from fastapi import APIRouter

from app.web.pages import router as pages_router
from app.web.photos import router as photos_router
from app.web.shows import router as shows_router
from app.web.stubs import router as stubs_router

router = APIRouter()
router.include_router(pages_router)
router.include_router(shows_router)
router.include_router(photos_router)
router.include_router(stubs_router)

__all__ = ["router"]
