from datetime import datetime

from sqlalchemy import Boolean, DateTime, Enum, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import CarStatus


class Car(Base):
    """One Entry — see CONTEXT.md. Entries are generated empty in a block
    at show creation (001..N, zero-padded) and filled in as whoever
    reaches them first (a judge in the field, or the host) enters
    Participant/Year/Make/Model — so every one of those fields is
    nullable, unlike the old model where they were required at creation.

    No `class_id` / CarClass relationship — Car Class is removed, see
    DECISIONS.md.

    `data_revision_at_change` replaces the old `updated_at`-based delta
    filtering: it's the Show.show_data_revision value at the moment this
    row last changed, and PROTOCOL.md's sync compares against that
    integer, not a timestamp — see services/sync.py.

    (Previously had `registration_number` + a separate `display_car_number`
    — collapsed into the one Entry Number the new spec defines. Previously
    had `owner_name` — renamed `participant` to match CONTEXT.md's
    workflow language.)
    """

    __tablename__ = "cars"
    __table_args__ = (UniqueConstraint("show_id", "entry_number", name="uq_car_show_entry_number"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    entry_number: Mapped[str] = mapped_column(String(10), nullable=False, index=True)

    participant: Mapped[str | None] = mapped_column(String(120), nullable=True)
    year: Mapped[str | None] = mapped_column(String(10), nullable=True)
    make: Mapped[str | None] = mapped_column(String(80), nullable=True)
    model: Mapped[str | None] = mapped_column(String(80), nullable=True)
    vehicle_type: Mapped[str | None] = mapped_column(String(40), nullable=True)
    # Set when the judge typed make/model rather than picking from the
    # approved vehicle list — feeds the (not-yet-built, see DECISIONS.md)
    # New Vehicle Names review queue. HB5.
    make_manually_entered: Mapped[bool] = mapped_column(Boolean, default=False, nullable=False)
    model_manually_entered: Mapped[bool] = mapped_column(Boolean, default=False, nullable=False)

    announcer_name: Mapped[str | None] = mapped_column(String(200), nullable=True)
    status: Mapped[CarStatus] = mapped_column(
        Enum(CarStatus, native_enum=False, validate_strings=True, length=32),
        default=CarStatus.UNJUDGED,
        nullable=False,
    )
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    data_revision_at_change: Mapped[int] = mapped_column(Integer, nullable=False)

    show: Mapped["Show"] = relationship(back_populates="cars")
    # Deleting a car explicitly takes its judging data with it — cascade="all,
    # delete-orphan" makes that cascade explicit at the ORM level rather than
    # relying on DB-level ON DELETE CASCADE, which SQLite's
    # PRAGMA foreign_keys=ON would otherwise reject as a constraint
    # violation on a bare car delete.
    submissions: Mapped[list["JudgingSubmission"]] = relationship(
        back_populates="car", cascade="all, delete-orphan"
    )
    photos: Mapped[list["Photo"]] = relationship(back_populates="car", cascade="all, delete-orphan")
