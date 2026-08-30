from datetime import datetime

from sqlalchemy import Boolean, DateTime, Enum, ForeignKey, Integer, String, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base, utcnow
from app.models.enums import AwardRankingBasis


class Award(Base):
    """A Show Award slot — see CONTEXT.md's Awards section. Distinct from
    Top Awards, which have no per-instance row at all (just
    Show.top_awards_count — there's nothing to name, nominate, or
    individually configure about "the top 50 cars").

    judge_chosen=True means judges nominate candidates on their handhelds
    (see AwardNomination) and Home Base picks the winner among nominees by
    ranking_basis. judge_chosen=False means ranking_basis is always None —
    the organiser picks winner_car_id directly via "Choose Winner," no
    scoring involved (Sponsor's Choice, memorial awards, etc.).

    winner_car_id is the single source of truth for what's actually
    announced — HB7 owns the logic that suggests or computes it; this
    model only stores the slot and (once set) the decision.

    (Previously had `category` ∈ {OVERALL, CRITERIA, CLASS} and a
    `class_id` FK. CLASS is gone — see DECISIONS.md, Car Class removal.
    Replaced by judge_chosen + ranking_basis, which map onto CONTEXT.md's
    "Which score should decide the winner?" question directly.)
    """

    __tablename__ = "awards"

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(120), nullable=False)
    judge_chosen: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)
    ranking_basis: Mapped[AwardRankingBasis | None] = mapped_column(
        Enum(AwardRankingBasis, native_enum=False, validate_strings=True, length=16), nullable=True
    )
    sort_order: Mapped[int] = mapped_column(Integer, default=0, nullable=False)
    winner_car_id: Mapped[int | None] = mapped_column(ForeignKey("cars.id"), nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)

    show: Mapped["Show"] = relationship()
    winner_car: Mapped["Car | None"] = relationship()
    nominations: Mapped[list["AwardNomination"]] = relationship(back_populates="award", cascade="all, delete-orphan")


class AwardNomination(Base):
    """A judge marking, on a submission, that its car should be
    considered for a judge-chosen Show Award — see CONTEXT.md: this
    decides eligibility only, never the winner. Linked to the submission
    (not the car directly) since a nomination is something a judge does
    at the moment of judging, the same way a score is."""

    __tablename__ = "award_nominations"
    __table_args__ = (UniqueConstraint("submission_id", "award_id", name="uq_nomination_submission_award"),)

    id: Mapped[int] = mapped_column(primary_key=True)
    submission_id: Mapped[int] = mapped_column(ForeignKey("judging_submissions.id"), nullable=False, index=True)
    award_id: Mapped[int] = mapped_column(ForeignKey("awards.id"), nullable=False, index=True)

    submission: Mapped["JudgingSubmission"] = relationship(back_populates="nominations")
    award: Mapped["Award"] = relationship(back_populates="nominations")
