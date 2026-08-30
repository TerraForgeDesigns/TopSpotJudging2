from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import PhotoStatus, PhotoType, TransferMethod


class Photo(Base):
    __tablename__ = "photos"

    id: Mapped[int] = mapped_column(primary_key=True)
    # show_id is set at ingest time — needed because an UNMATCHED photo has
    # no car to derive a show from (same reasoning as JudgingSubmission).
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    # Nullable: a photo whose registration number matched no car
    # (PhotoStatus.UNMATCHED) has no car to point at yet.
    car_id: Mapped[int | None] = mapped_column(ForeignKey("cars.id"), nullable=True, index=True)
    # What the filename (or WiFi upload form field) actually said — kept
    # even after resolution, independent of the car's current registration
    # number, which may later be edited.
    registration_number: Mapped[str] = mapped_column(String(40), nullable=False, index=True)
    photo_type: Mapped[PhotoType] = mapped_column(
        Enum(PhotoType, native_enum=False, validate_strings=True, length=32), nullable=False
    )
    status: Mapped[PhotoStatus] = mapped_column(
        Enum(PhotoStatus, native_enum=False, validate_strings=True, length=16), nullable=False
    )
    source_handheld_id: Mapped[int | None] = mapped_column(ForeignKey("handhelds.id"), nullable=True, index=True)
    transfer_method: Mapped[TransferMethod] = mapped_column(
        Enum(TransferMethod, native_enum=False, validate_strings=True, length=16), nullable=False
    )
    transferred_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    # Both paths are relative to PHOTOS_DIR (app/config.py), not absolute —
    # portable if the whole data/ directory is copied elsewhere.
    file_path: Mapped[str] = mapped_column(String(500), nullable=False)
    thumbnail_path: Mapped[str | None] = mapped_column(String(500), nullable=True)
    original_filename: Mapped[str] = mapped_column(String(255), nullable=False)

    car: Mapped["Car | None"] = relationship(back_populates="photos")
    source_handheld: Mapped["Handheld | None"] = relationship(back_populates="photos")
