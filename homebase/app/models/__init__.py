"""
Import every model module here so their classes register on Base's mapper
registry before any relationship() string reference is resolved, and so
Alembic autogenerate sees the complete schema from a single import.
"""
from app.models.app_settings import AppSettings
from app.models.award import Award, AwardNomination
from app.models.base import Base
from app.models.car import Car
from app.models.category import JudgingCategory
from app.models.enums import (
    AwardRankingBasis,
    CarStatus,
    PhotoStatus,
    PhotoType,
    SubmissionStatus,
    TransferMethod,
    VehicleCandidateStatus,
    VehicleSource,
)
from app.models.handheld import Handheld
from app.models.photo import Photo
from app.models.show import Show
from app.models.submission import JudgingScore, JudgingSubmission
from app.models.vehicle import VehicleCandidate, VehicleCandidateSighting, VehicleMake, VehicleModel

__all__ = [
    "Base",
    "AppSettings",
    "Show",
    "Car",
    "JudgingCategory",
    "JudgingSubmission",
    "JudgingScore",
    "Photo",
    "Handheld",
    "Award",
    "AwardNomination",
    "VehicleMake",
    "VehicleModel",
    "VehicleCandidate",
    "VehicleCandidateSighting",
    "CarStatus",
    "SubmissionStatus",
    "PhotoType",
    "PhotoStatus",
    "TransferMethod",
    "AwardRankingBasis",
    "VehicleSource",
    "VehicleCandidateStatus",
]
