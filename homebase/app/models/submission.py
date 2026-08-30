from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import SubmissionStatus


class JudgingSubmission(Base):
    """One judge's closed-out scoring of one car — see PROTOCOL.md's
    POST /api/v1/sync `submissions` array and CONTEXT.md's workflow.

    car_id is NOT NULL (unlike the old model): entries are pre-created
    001..N at show creation, so a submission's entry_number always
    resolves to an existing Car or the whole item is rejected outright
    (see services/sync.py) — there is no more "held, unmatched" state for
    submissions specifically (see SubmissionStatus).

    Idempotency: (entry_number, handheld_id, closed_at_uptime_ms) is the
    retry key PROTOCOL.md defines — a resend with the same triple after a
    lost ack must be recognized as the SAME submission, never a fresh
    conflict. Enforced at the DB level as a safety net in addition to the
    explicit check in services/sync.py.

    closed_at_uptime_ms is the raw value the handheld sent (milliseconds
    since its own boot — it has no wall clock before its first sync of
    the day); closed_at is Home Base's conversion of that to a real
    timestamp at receipt. Both are kept — see PROTOCOL.md.

    Overall Impression (CONTEXT.md) is optional and tie-break-only, so it
    gets the same original/adjusted audit-trail treatment as a category
    score (see JudgingScore below) but lives directly on the submission
    since there's only ever one of it per car, not one per category.
    """

    __tablename__ = "judging_submissions"
    __table_args__ = (
        UniqueConstraint(
            "entry_number", "handheld_id", "closed_at_uptime_ms", name="uq_submission_idempotency"
        ),
    )

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    car_id: Mapped[int] = mapped_column(ForeignKey("cars.id"), nullable=False, index=True)
    entry_number: Mapped[str] = mapped_column(String(10), nullable=False, index=True)
    handheld_id: Mapped[int] = mapped_column(ForeignKey("handhelds.id"), nullable=False, index=True)
    judge_name: Mapped[str | None] = mapped_column(String(120), nullable=True)
    # Set when a host enters a corrected score set to resolve a conflict —
    # the rationale for overriding what the judges actually submitted.
    # Null for ordinary handheld submissions.
    note: Mapped[str | None] = mapped_column(String(500), nullable=True)

    closed_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    closed_at_uptime_ms: Mapped[int] = mapped_column(Integer, nullable=False)
    submitted_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    status: Mapped[SubmissionStatus] = mapped_column(
        Enum(SubmissionStatus, native_enum=False, validate_strings=True, length=32),
        default=SubmissionStatus.ACCEPTED,
        nullable=False,
    )

    overall_impression_original_points: Mapped[int | None] = mapped_column(Integer, nullable=True)
    overall_impression_original_range_max: Mapped[int | None] = mapped_column(Integer, nullable=True)
    overall_impression_adjusted_points: Mapped[int | None] = mapped_column(Integer, nullable=True)

    car: Mapped["Car"] = relationship(back_populates="submissions")
    handheld: Mapped["Handheld"] = relationship(back_populates="submissions")
    scores: Mapped[list["JudgingScore"]] = relationship(back_populates="submission", cascade="all, delete-orphan")
    nominations: Mapped[list["AwardNomination"]] = relationship(
        back_populates="submission", cascade="all, delete-orphan"
    )


class JudgingScore(Base):
    """One category's score within a submission. Stores the full
    conversion audit trail CONTEXT.md requires: original_points and
    original_range_max are exactly what the judge entered and never
    change; adjusted_points is what every ranking, award, display, and
    export actually uses, recomputed via services/score_conversion.py
    whenever the show's range escalates — always from the original, never
    from a previously adjusted value. See CONTEXT.md's conversion tables
    and services/range_escalation.py.
    """

    __tablename__ = "judging_scores"

    id: Mapped[int] = mapped_column(primary_key=True)
    submission_id: Mapped[int] = mapped_column(ForeignKey("judging_submissions.id"), nullable=False, index=True)
    category_id: Mapped[int] = mapped_column(ForeignKey("judging_categories.id"), nullable=False, index=True)

    original_points: Mapped[int] = mapped_column(Integer, nullable=False)
    original_range_max: Mapped[int] = mapped_column(Integer, nullable=False)
    adjusted_points: Mapped[int] = mapped_column(Integer, nullable=False)

    submission: Mapped["JudgingSubmission"] = relationship(back_populates="scores")
    category: Mapped["JudgingCategory"] = relationship(back_populates="scores")
