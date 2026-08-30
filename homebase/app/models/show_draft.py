from datetime import datetime

from sqlalchemy import JSON, DateTime
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, utcnow


class ShowDraft(Base):
    """In-progress state for the Create Show wizard — see
    services/show_wizard.py. This is what "keep wizard state server-side
    against a draft, not in hidden form fields" means concretely: every
    wizard screen reads and writes THIS row (via draft_id in the URL),
    never round-trips the show/categories/awards state through the
    browser as hidden inputs, and moving Back never loses anything
    because nothing was ever only-in-the-browser to begin with.

    `data` is a single JSON blob rather than a set of normalized draft
    tables (DraftCategory, DraftAward, ...) — deliberately: this state is
    thrown away the moment services/show_wizard.py materializes it into a
    real Show (see delete after creation), so there's no query, report,
    or foreign key ever built against a draft's internals. A blob that's
    read and rewritten as a whole is simpler and cheaper here than
    modeling and migrating tables for data with no life beyond one
    browser session. See DECISIONS.md.

    Shape of `data` (all populated with defaults when the draft is
    created — see show_wizard.py's new_draft_data()):
      name, event_date, location, notes
      car_count
      categories: [{id, name, active, sort_order}, ...]        (5 built-ins, fixed set)
      overall_impression_enabled
      top_awards_count
      awards: [{id, name, builtin, active, judge_chosen, ranking_basis, sort_order}, ...]
      next_award_id   (local counter for assigning ids to added awards)
    """

    __tablename__ = "show_drafts"

    id: Mapped[int] = mapped_column(primary_key=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, nullable=False)
    data: Mapped[dict] = mapped_column(JSON, nullable=False, default=dict)
