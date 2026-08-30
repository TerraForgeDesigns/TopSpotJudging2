"""
Wire-format models for /api/v1 — field names and shapes here are the
contract in /PROTOCOL.md. Don't rename fields without updating that
document; the firmware is built against it independently.
"""
from datetime import datetime

from pydantic import BaseModel, Field


class ScoreIn(BaseModel):
    criteria_name: str
    points: int


class SubmissionIn(BaseModel):
    registration_number: str
    judge_name: str | None = None
    closed_at: datetime
    scores: list[ScoreIn] = Field(default_factory=list)


class SubmissionsRequest(BaseModel):
    handheld_id: str
    since: datetime | None = None
    submissions: list[SubmissionIn] = Field(default_factory=list)


class SummaryOut(BaseModel):
    total_cars: int
    judged: int
    unjudged: int
    flagged_conflict: int


class CarOut(BaseModel):
    id: int
    registration_number: str
    display_car_number: str
    make: str
    model: str
    year: int
    class_name: str | None
    status: str
    updated_at: datetime


class ClassOut(BaseModel):
    id: int
    name: str


class CriteriaOut(BaseModel):
    id: int
    name: str
    max_points: int


class RosterResponse(BaseModel):
    server_time: datetime
    cars: list[CarOut]
    classes: list[ClassOut]
    criteria: list[CriteriaOut]
    summary: SummaryOut


class ResultItem(BaseModel):
    registration_number: str
    status: str  # "accepted" | "flagged_duplicate" | "error" — see PROTOCOL.md
    message: str = ""


class SubmissionsResponse(BaseModel):
    server_time: datetime
    results: list[ResultItem]
    roster_delta: RosterResponse
    summary: SummaryOut


class HealthResponse(BaseModel):
    server_time: datetime
    show_name: str | None
