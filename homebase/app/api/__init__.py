"""
JSON API routes (the handheld-facing surface defined in /PROTOCOL.md).
Not yet implemented — this session scaffolds structure only.
"""
from fastapi import APIRouter

router = APIRouter(prefix="/api/v1")


@router.get("/health")
def health() -> dict:
    return {"status": "ok"}
