"""
Import every model module here so their classes register on Base's mapper
registry before any relationship() string reference is resolved, and so
Alembic autogenerate sees the complete schema from a single import.
"""
from app.models.app_settings import AppSettings
from app.models.award import Award
from app.models.base import Base
from app.models.car import Car
from app.models.criteria import JudgingCriteria
from app.models.enums import (
    AwardCategory,
    CarStatus,
    PhotoStatus,
    PhotoType,
    SubmissionStatus,
    TransferMethod,
)
from app.models.handheld import Handheld
from app.models.photo import Photo
from app.models.show import Show
from app.models.submission import JudgingScore, JudgingSubmission

# NOTE: this file is mid-reconciliation against the Aug 2026 spec update —
# CarClass is gone (step 1 of that reconciliation; see DECISIONS.md) but
# the rest of these models still describe the OLD schema pending step 2
# (full model rewrite), which follows immediately after this commit.
__all__ = [
    "Base",
    "AppSettings",
    "Show",
    "Car",
    "JudgingCriteria",
    "JudgingSubmission",
    "JudgingScore",
    "Photo",
    "Handheld",
    "Award",
    "CarStatus",
    "SubmissionStatus",
    "PhotoType",
    "PhotoStatus",
    "TransferMethod",
    "AwardCategory",
]
