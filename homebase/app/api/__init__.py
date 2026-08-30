"""
JSON API routes (the handheld-facing surface defined in /PROTOCOL.md).
"""
from fastapi import APIRouter

from app.api.photos import router as photos_router
from app.api.sync import router as sync_router

router = APIRouter(prefix="/api/v1")
router.include_router(sync_router)
router.include_router(photos_router)
