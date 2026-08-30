"""
JSON API routes (the handheld-facing surface defined in /PROTOCOL.md).
"""
from fastapi import APIRouter

from app.api.sync import router as sync_router

router = APIRouter(prefix="/api/v1")
router.include_router(sync_router)
