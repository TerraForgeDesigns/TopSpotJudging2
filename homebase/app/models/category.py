from sqlalchemy import Boolean, ForeignKey, Integer, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.models.base import Base


class JudgingCategory(Base):
    """One of the five built-in Judging Categories (Engine, Exterior,
    Interior, Paint, Wheels / Tires) — see CONTEXT.md. The set is fixed;
    there is no create operation and none is planned. `active` toggles
    whether a category counts toward Max Score and appears on handhelds.
    `sort_order` does double duty as the organiser's category priority
    order for tie-breaking (CONTEXT.md's tie-break cascade, step 3) — the
    same ordering concept, reused rather than duplicated. This is a real
    trade-off, not a free simplification: an organiser cannot have a
    category display third to judges but break ties first, since
    reordering one always reorders the other. Accepted deliberately — see
    DECISIONS.md — but anything that lets an organiser drag this order
    (the wizard's Judging Setup step, and any future post-creation
    editor) must say plainly that it changes both.

    No `max_points` (the old model had one) — the range is automatic and
    show-wide, not per-category; see Show.score_range_max. No
    `updated_at` either — sync no longer compares per-row timestamps, see
    Show.configuration_revision.

    `built_in_key` is the category's STABLE identity ("engine", "paint",
    etc. — the same strings services/show_wizard.py's BUILTIN_CATEGORIES
    and models.enums.AwardRankingBasis use), set once at show creation and
    never changed even if the organiser renames the category. Without it,
    an Award whose ranking_basis is ENGINE would have no reliable way to
    find "the Engine category" once it's been renamed to "Motor" — a
    silently wrong award winner, not a real one. `name` is what's shown
    to judges and the organiser; `built_in_key` is what code matches on.
    Nullable only so a row created before this field existed (dev data)
    degrades to "not usable as an award ranking basis" instead of failing
    to load — see services/award_results.py.

    (Previously named JudgingCriteria — renamed to match LANGUAGE.md.)
    """

    __tablename__ = "judging_categories"

    id: Mapped[int] = mapped_column(primary_key=True)
    show_id: Mapped[int] = mapped_column(ForeignKey("shows.id"), nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(120), nullable=False)
    built_in_key: Mapped[str | None] = mapped_column(String(20), nullable=True)
    sort_order: Mapped[int] = mapped_column(Integer, default=0, nullable=False)
    active: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)

    show: Mapped["Show"] = relationship(back_populates="categories")
    scores: Mapped[list["JudgingScore"]] = relationship(back_populates="category")
