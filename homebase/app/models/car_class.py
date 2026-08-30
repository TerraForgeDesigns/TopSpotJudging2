from sqlalchemy import ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base


class CarClass(Base):
    __tablename__ = "car_classes"

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(120), nullable=False)
    sort_order: Mapped[int] = mapped_column(Integer, default=0, nullable=False)

    show: Mapped["Show"] = relationship(back_populates="classes")
    cars: Mapped[list["Car"]] = relationship(back_populates="car_class")
