from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import PhotoType, TransferMethod


class Photo(Base):
    __tablename__ = "photos"

    id: Mapped[int] = mapped_column(primary_key=True)
    car_id: Mapped[int] = mapped_column(ForeignKey("cars.id"), nullable=False, index=True)
    photo_type: Mapped[PhotoType] = mapped_column(
        Enum(PhotoType, native_enum=False, validate_strings=True, length=32), nullable=False
    )
    source_handheld_id: Mapped[int | None] = mapped_column(ForeignKey("handhelds.id"), nullable=True, index=True)
    transfer_method: Mapped[TransferMethod] = mapped_column(
        Enum(TransferMethod, native_enum=False, validate_strings=True, length=16), nullable=False
    )
    transferred_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    file_path: Mapped[str] = mapped_column(String(500), nullable=False)

    car: Mapped["Car"] = relationship(back_populates="photos")
    source_handheld: Mapped["Handheld | None"] = relationship(back_populates="photos")
