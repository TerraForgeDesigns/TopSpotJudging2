from datetime import datetime

from sqlalchemy import Boolean, DateTime, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow


class JudgingCriteria(Base):
    __tablename__ = "judging_criteria"

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(120), nullable=False)
    max_points: Mapped[int] = mapped_column(Integer, nullable=False)
    sort_order: Mapped[int] = mapped_column(Integer, default=0, nullable=False)
    active: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)
    # Delta sync (PROTOCOL.md GET /sync/roster?since=) relies on this changing on
    # every update. onupdate= fires on any SQLAlchemy-issued UPDATE to this row.
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow, nullable=False
    )

    show: Mapped["Show"] = relationship(back_populates="criteria")
    scores: Mapped[list["JudgingScore"]] = relationship(back_populates="criteria")
