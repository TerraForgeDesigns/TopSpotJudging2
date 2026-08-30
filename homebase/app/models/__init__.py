"""
Import every model module here so their classes register on Base's mapper
registry before any relationship() string reference is resolved, and so
Alembic autogenerate sees the complete schema from a single import.
"""
from app.models.app_settings import AppSettings
from app.models.base import Base
from app.models.car import Car
from app.models.car_class import CarClass
from app.models.criteria import JudgingCriteria
from app.models.enums import CarStatus, PhotoStatus, PhotoType, SubmissionStatus, TransferMethod
from app.models.handheld import Handheld
from app.models.photo import Photo
from app.models.show import Show
from app.models.submission import JudgingScore, JudgingSubmission

__all__ = [
    "Base",
    "AppSettings",
    "Show",
    "CarClass",
    "Car",
    "JudgingCriteria",
    "JudgingSubmission",
    "JudgingScore",
    "Photo",
    "Handheld",
    "CarStatus",
    "SubmissionStatus",
    "PhotoType",
    "PhotoStatus",
    "TransferMethod",
]
