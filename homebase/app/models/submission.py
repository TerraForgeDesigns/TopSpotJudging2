from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import SubmissionStatus


class JudgingSubmission(Base):
    __tablename__ = "judging_submissions"

    id: Mapped[int] = mapped_column(primary_key=True)
    # show_id is set from whichever show was active when home base received
    # this submission — needed because an UNMATCHED submission has no car,
    # and therefore no other way to scope it to a show for reconciliation.
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    # Nullable: a submission whose registration_number doesn't match any car
    # yet (SubmissionStatus.UNMATCHED) has no car to point at. registration_number
    # is stored on every submission regardless — it's what the handheld actually
    # sent, independent of a car record that may not exist yet or may later be
    # edited (see services/sync.py and PROTOCOL.md).
    car_id: Mapped[int | None] = mapped_column(ForeignKey("cars.id"), nullable=True, index=True)
    registration_number: Mapped[str] = mapped_column(String(40), nullable=False, index=True)
    handheld_id: Mapped[int] = mapped_column(ForeignKey("handhelds.id"), nullable=False, index=True)
    judge_name: Mapped[str | None] = mapped_column(String(120), nullable=True)
    closed_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    submitted_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    status: Mapped[SubmissionStatus] = mapped_column(
        Enum(SubmissionStatus, native_enum=False, validate_strings=True, length=32),
        default=SubmissionStatus.PENDING,
        nullable=False,
    )

    car: Mapped["Car | None"] = relationship(back_populates="submissions")
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
