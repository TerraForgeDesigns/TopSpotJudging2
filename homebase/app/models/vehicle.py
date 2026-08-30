"""
The vehicle make/model vocabulary — see CONTEXT.md and DECISIONS.md
("SPEC-C", not yet written, is what fully specifies how these get used).
Models only, per this task's scope — the services that populate,
prepare, and review these live in HB5. Defined now purely so the single
initial migration (see DECISIONS.md) covers the complete schema and a
second migration isn't needed a few prompts from now.

`source` on VehicleMake/VehicleModel is load-bearing, not decorative: it
keeps names a real judge typed in at a real show (LEARNED) permanently
separable from the bundled seed list (SEED), so replacing or updating the
seed list can never silently delete something an installation actually
learned. Do not collapse it into a boolean or drop it because nothing
reads it yet — HB5 depends on this distinction existing from day one.
"""
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Enum, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import VehicleCandidateStatus, VehicleSource


class VehicleMake(Base):
    __tablename__ = "vehicle_makes"

    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(80), unique=True, nullable=False)
    source: Mapped[VehicleSource] = mapped_column(
        Enum(VehicleSource, native_enum=False, validate_strings=True, length=16), nullable=False
    )
    active: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)

    models: Mapped[list["VehicleModel"]] = relationship(back_populates="make")


class VehicleModel(Base):
    __tablename__ = "vehicle_models"
    __table_args__ = (UniqueConstraint("make_id", "name", name="uq_vehicle_model_make_name"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    make_id: Mapped[int] = mapped_column(ForeignKey("vehicle_makes.id"), nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(80), nullable=False)
    source: Mapped[VehicleSource] = mapped_column(
        Enum(VehicleSource, native_enum=False, validate_strings=True, length=16), nullable=False
    )
    active: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)

    make: Mapped["VehicleMake"] = relationship(back_populates="models")


class VehicleCandidate(Base):
    """A make/model pairing seen on an entry that isn't in the approved
    vehicle list yet — the New Vehicle Names review queue's raw material.
    Built up across shows (times_seen, first/last_seen_at) so HB5's
    review UI can prioritize by frequency. normalized_key is whatever
    case/whitespace-insensitive dedup key HB5's preparation script
    defines — enforced unique here so re-sighting the same pairing is
    always an update, never a duplicate row."""

    __tablename__ = "vehicle_candidates"

    id: Mapped[int] = mapped_column(primary_key=True)
    make_name: Mapped[str] = mapped_column(String(80), nullable=False)
    model_name: Mapped[str] = mapped_column(String(80), nullable=False)
    normalized_key: Mapped[str] = mapped_column(String(200), unique=True, nullable=False, index=True)
    times_seen: Mapped[int] = mapped_column(Integer, default=1, nullable=False)
    status: Mapped[VehicleCandidateStatus] = mapped_column(
        Enum(VehicleCandidateStatus, native_enum=False, validate_strings=True, length=16),
        default=VehicleCandidateStatus.PENDING,
        nullable=False,
    )
    merged_into_model_id: Mapped[int | None] = mapped_column(ForeignKey("vehicle_models.id"), nullable=True)
    first_seen_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    last_seen_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    merged_into_model: Mapped["VehicleModel | None"] = relationship()
    sightings: Mapped[list["VehicleCandidateSighting"]] = relationship(
        back_populates="candidate", cascade="all, delete-orphan"
    )


class VehicleCandidateSighting(Base):
    """One occurrence of a VehicleCandidate on one entry, at one show —
    the audit trail behind times_seen, and what HB5's review UI would
    show as "seen on entry 042 at the Fall Cruise-In"."""

    __tablename__ = "vehicle_candidate_sightings"

    id: Mapped[int] = mapped_column(primary_key=True)
    candidate_id: Mapped[int] = mapped_column(ForeignKey("vehicle_candidates.id"), nullable=False, index=True)
    entry_id: Mapped[int] = mapped_column(ForeignKey("cars.id"), nullable=False, index=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    seen_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    candidate: Mapped["VehicleCandidate"] = relationship(back_populates="sightings")
