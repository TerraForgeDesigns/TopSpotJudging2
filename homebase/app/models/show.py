from datetime import date, datetime

from sqlalchemy import Boolean, Date, DateTime, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow


class Show(Base):
    """See CONTEXT.md's "Judging categories & scoring range" and "Awards"
    sections — score_range_max/configuration_revision/show_data_revision
    are the load-bearing new fields here.

    score_range_max is the show's CURRENT per-category range ceiling (5,
    10, or 25) — see services/range_escalation.py. It only ever moves up;
    nothing in this codebase may decrease it once set.

    configuration_revision / show_data_revision are the two monotonically
    increasing integers PROTOCOL.md's sync contract is built on, in place
    of timestamps — see services/revisions.py and DECISIONS.md for why.
    """

    __tablename__ = "shows"

    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(200), nullable=False)
    event_date: Mapped[date] = mapped_column(Date, nullable=False)
    location: Mapped[str] = mapped_column(String(200), nullable=False)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    score_range_max: Mapped[int] = mapped_column(Integer, default=5, nullable=False)
    overall_impression_enabled: Mapped[bool] = mapped_column(Boolean, default=False, nullable=False)
    # One of 10/20/50/100/150/200, or None if the organiser hasn't set
    # Top Awards up yet — see CONTEXT.md's Awards section. Auto-computed
    # winners (who's actually in the top N) are HB7's concern; this is
    # just the organiser's chosen count.
    top_awards_count: Mapped[int | None] = mapped_column(Integer, nullable=True)

    configuration_revision: Mapped[int] = mapped_column(Integer, default=1, nullable=False)
    show_data_revision: Mapped[int] = mapped_column(Integer, default=1, nullable=False)

    categories: Mapped[list["JudgingCategory"]] = relationship(back_populates="show")
    cars: Mapped[list["Car"]] = relationship(back_populates="show")
