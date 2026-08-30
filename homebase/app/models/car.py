from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import CarStatus


class Car(Base):
    __tablename__ = "cars"

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    registration_number: Mapped[str] = mapped_column(String(40), unique=True, index=True, nullable=False)
    display_car_number: Mapped[str] = mapped_column(String(40), nullable=False)
    make: Mapped[str] = mapped_column(String(80), nullable=False)
    model: Mapped[str] = mapped_column(String(80), nullable=False)
    year: Mapped[int] = mapped_column(Integer, nullable=False)
    owner_name: Mapped[str | None] = mapped_column(String(120), nullable=True)
    announcer_name: Mapped[str | None] = mapped_column(String(200), nullable=True)
    class_id: Mapped[int | None] = mapped_column(ForeignKey("car_classes.id"), nullable=True, index=True)
    status: Mapped[CarStatus] = mapped_column(
        Enum(CarStatus, native_enum=False, validate_strings=True, length=32),
        default=CarStatus.UNJUDGED,
        nullable=False,
    )
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    # Delta sync (PROTOCOL.md GET /sync/roster?since=) relies on this changing on
    # every update. onupdate= fires on any SQLAlchemy-issued UPDATE to this row.
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow, nullable=False
    )

    show: Mapped["Show"] = relationship(back_populates="cars")
    car_class: Mapped["CarClass | None"] = relationship(back_populates="cars")
    # Deleting a car explicitly takes its judging data with it (see
    # services/cars.py delete_car, which requires a confirmation naming what's
    # lost before this ever fires) — cascade="all, delete-orphan" makes that
    # cascade explicit at the ORM level rather than relying on DB-level
    # ON DELETE CASCADE, which SQLite's PRAGMA foreign_keys=ON would otherwise
    # reject as a constraint violation on a bare car delete.
    submissions: Mapped[list["JudgingSubmission"]] = relationship(
        back_populates="car", cascade="all, delete-orphan"
    )
    photos: Mapped[list["Photo"]] = relationship(back_populates="car", cascade="all, delete-orphan")
