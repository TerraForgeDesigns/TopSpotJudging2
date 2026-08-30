from sqlalchemy import ForeignKey
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class AppSettings(Base):
    """Singleton row (id=1) holding home-base-wide state — currently just
    which show is active. Everything else in the app operates on the active
    show; see CONTEXT.md."""

    __tablename__ = "app_settings"

    id: Mapped[int] = mapped_column(primary_key=True)
    active_show_id: Mapped[int | None] = mapped_column(ForeignKey("shows.id"), nullable=True)
