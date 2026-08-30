from datetime import datetime

from sqlalchemy import DateTime, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base


class Handheld(Base):
    __tablename__ = "handhelds"

    id: Mapped[int] = mapped_column(primary_key=True)
    # The wire protocol's handheld_id (see PROTOCOL.md) is a string the
    # firmware assigns itself, not this row's integer PK — label is the
    # match key, auto-provisioning a new Handheld row the first time an
    # unseen label calls in. See services/sync.py get_or_create_handheld.
    label: Mapped[str] = mapped_column(String(80), unique=True, nullable=False)
    firmware_version: Mapped[str | None] = mapped_column(String(40), nullable=True)
    last_sync_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    last_ip: Mapped[str | None] = mapped_column(String(45), nullable=True)
    battery_pct: Mapped[int | None] = mapped_column(Integer, nullable=True)

    submissions: Mapped[list["JudgingSubmission"]] = relationship(back_populates="handheld")
    photos: Mapped[list["Photo"]] = relationship(back_populates="source_handheld")
