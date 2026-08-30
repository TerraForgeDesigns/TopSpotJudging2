from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import SubmissionStatus


class JudgingSubmission(Base):
    __tablename__ = "judging_submissions"

    id: Mapped[int] = mapped_column(primary_key=True)
    car_id: Mapped[int] = mapped_column(ForeignKey("cars.id"), nullable=False, index=True)
    handheld_id: Mapped[int] = mapped_column(ForeignKey("handhelds.id"), nullable=False, index=True)
    judge_name: Mapped[str | None] = mapped_column(String(120), nullable=True)
    submitted_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    status: Mapped[SubmissionStatus] = mapped_column(
        Enum(SubmissionStatus, native_enum=False, validate_strings=True, length=32),
        default=SubmissionStatus.PENDING,
        nullable=False,
    )

    car: Mapped["Car"] = relationship(back_populates="submissions")
    handheld: Mapped["Handheld"] = relationship(back_populates="submissions")
    scores: Mapped[list["JudgingScore"]] = relationship(back_populates="submission", cascade="all, delete-orphan")


class JudgingScore(Base):
    __tablename__ = "judging_scores"

    id: Mapped[int] = mapped_column(primary_key=True)
    submission_id: Mapped[int] = mapped_column(ForeignKey("judging_submissions.id"), nullable=False, index=True)
    criteria_id: Mapped[int] = mapped_column(ForeignKey("judging_criteria.id"), nullable=False, index=True)
    points: Mapped[int] = mapped_column(Integer, nullable=False)

    submission: Mapped["JudgingSubmission"] = relationship(back_populates="scores")
    criteria: Mapped["JudgingCriteria"] = relationship(back_populates="scores")
