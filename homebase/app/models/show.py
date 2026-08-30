from datetime import date, datetime

from sqlalchemy import Date, DateTime, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow


class Show(Base):
    __tablename__ = "shows"

    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(200), nullable=False)
    event_date: Mapped[date] = mapped_column(Date, nullable=False)
    location: Mapped[str] = mapped_column(String(200), nullable=False)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    classes: Mapped[list["CarClass"]] = relationship(back_populates="show")
    cars: Mapped[list["Car"]] = relationship(back_populates="show")
    criteria: Mapped[list["JudgingCriteria"]] = relationship(back_populates="show")
