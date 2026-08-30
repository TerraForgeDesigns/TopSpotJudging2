from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import AwardCategory


class Award(Base):
    """A named award slot (e.g. "Best in Show", "Best Paint", "Best in
    Class: Trucks") a host defines ahead of the ceremony. The winner is
    auto-suggested from the relevant ranking at display time (see
    services/awards.py) and stored here only once the host confirms or
    overrides it — winner_car_id is the single source of truth for
    results/awards presentation."""

    __tablename__ = "awards"

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(120), nullable=False)
    category: Mapped[AwardCategory] = mapped_column(
        Enum(AwardCategory, native_enum=False, validate_strings=True, length=16), nullable=False
    )
    # Only meaningful for category == CRITERIA / CLASS respectively.
    criteria_id: Mapped[int | None] = mapped_column(ForeignKey("judging_criteria.id"), nullable=True)
    class_id: Mapped[int | None] = mapped_column(ForeignKey("car_classes.id"), nullable=True)
    sort_order: Mapped[int] = mapped_column(Integer, default=0, nullable=False)
    winner_car_id: Mapped[int | None] = mapped_column(ForeignKey("cars.id"), nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    show: Mapped["Show"] = relationship()
    criteria: Mapped["JudgingCriteria | None"] = relationship()
    car_class: Mapped["CarClass | None"] = relationship()
    winner_car: Mapped["Car | None"] = relationship()
