"""
Wire-format models for /api/v1 — field names and shapes here are the
contract in /PROTOCOL.md. Don't rename fields without updating that
document; the firmware is built against it independently.
"""
from datetime import datetime

from pydantic import BaseModel, Field


class ScoreIn(BaseModel):
    category_id: int
    points: int


class SubmissionIn(BaseModel):
    local_record_id: str | None = None
    entry_number: str
    judge_name: str | None = None
    closed_at_uptime_ms: int
    participant: str | None = None
    year: str | None = None
    make: str | None = None
    model: str | None = None
    vehicle_type: str | None = None
    make_manually_entered: bool = False
    model_manually_entered: bool = False
    score_range_max: int  # REQUIRED — see PROTOCOL.md
    scores: list[ScoreIn] = Field(default_factory=list)
    overall_impression: int | None = None
    nominations: list[int] = Field(default_factory=list)  # award ids
    vehicle_photo_path: str | None = None
    judge_sheet_photo_path: str | None = None


class SyncRequest(BaseModel):
    handheld_id: str
    show_id: int = Field(default=0, ge=0)
    config_revision: int
    data_revision: int
    known_car_count: int = 0
    battery_pct: int | None = None
    firmware_version: str | None = None
    protocol_version: int | None = None
    rssi_dbm: int | None = None
    submissions: list[SubmissionIn] = Field(default_factory=list)


class HandshakeRequest(BaseModel):
    handheld_id: str
    firmware_version: str | None = None
    protocol_version: int = 1
    rssi_dbm: int | None = None


class HandshakeResponse(BaseModel):
    service: str
    status: str
    protocol_version: int
    server_time: datetime
    show_name: str | None
    handheld_id: str


class ResultItem(BaseModel):
    entry_number: str
    status: str  # "accepted" | "already_recorded" | "flagged_duplicate" | "error" — see PROTOCOL.md
    message: str = ""


class ProtocolSummary(BaseModel):
    """Exactly the four fields PROTOCOL.md's `summary` object specifies —
    owned by the wire contract, nothing more.

    CHANGING THIS TYPE CHANGES THE FIRMWARE CONTRACT. It is deliberately
    NOT the same type as services/dashboard.py's dashboard-facing summary
    (DashboardSummary), even though today both are computed from the same
    Car.status counts — see DECISIONS.md ("shared-by-coincidence types
    are how wire contracts get broken silently"). If the Show Dashboard
    needs a fifth stat tomorrow, DashboardSummary grows; this does not,
    unless PROTOCOL.md itself changes.
    """

    total_cars: int
    judged: int
    unjudged: int
    flagged_conflict: int


class CategoryOut(BaseModel):
    id: int
    name: str
    sort_order: int


class NominationOptionOut(BaseModel):
    """A judge-chosen Show Award, as far as a handheld needs to know
    about it to offer it as a nomination checkbox. Winner-resolution
    logic (ranking basis, tie cascade, etc.) is HB7's concern — this is
    intentionally minimal."""

    id: int
    name: str


class ConfigurationOut(BaseModel):
    show_id: int
    show_name: str
    show_date: str | None = None
    config_revision: int
    active: bool = True
    score_range_max: int
    # Computed server-side (active categories x score_range_max) so a
    # handheld never derives it independently and never disagrees with
    # Home Base — see CONTEXT.md's Max Score definition and PROTOCOL.md.
    max_score: int
    overall_impression_enabled: bool
    categories: list[CategoryOut]
    judge_chosen_awards: list[NominationOptionOut] = Field(default_factory=list)


class CarOut(BaseModel):
    id: int
    entry_number: str
    participant: str | None
    year: str | None
    make: str | None
    model: str | None
    vehicle_type: str | None
    status: str
    data_revision: int
    judged_source: str | None = None
    judged_at: datetime | None = None


class SyncResponse(BaseModel):
    server_time: datetime
    config_revision: int
    data_revision: int
    sync_mode: str
    configuration: ConfigurationOut | None
    cars: list[CarOut]
    # Approved vehicle names — full semantics defined by SPEC-B (the
    # master build guide's three-layer vehicle database, review queue,
    # and vehicle_additions spec), not yet implemented here (HB5 builds
    # the services). This field's presence in the contract is settled;
    # its behavior isn't wired up yet. See PROTOCOL.md/DECISIONS.md.
    vehicle_additions: list[dict] = Field(default_factory=list)
    results: list[ResultItem]
    summary: ProtocolSummary


class HealthResponse(BaseModel):
    service: str
    status: str
    protocol_version: int
    server_time: datetime
    show_name: str | None
