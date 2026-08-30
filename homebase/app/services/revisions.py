"""
The two sync revision counters — see PROTOCOL.md and CONTEXT.md's Show
Setup section for exactly what bumps which one:

  configuration_revision — any Show Setup change: categories, awards,
    award settings, score range escalation, category priority order.
  show_data_revision — any change to cars: entries added, participant or
    vehicle details entered or corrected.

Handhelds have no battery-backed clock and take their time from Home
Base, so comparing these two monotonically increasing integers is more
reliable across devices than comparing timestamps — see DECISIONS.md.

These functions mutate and flush but deliberately do NOT commit — a
revision bump is almost always one part of a larger transaction (e.g.
range escalation touches many JudgingScore rows in the same commit), and
committing here would either be premature or redundant depending on the
caller. The caller commits.
"""
from sqlalchemy.orm import Session

from app.models import Show


def bump_configuration_revision(db: Session, show: Show) -> int:
    show.configuration_revision += 1
    db.flush()
    return show.configuration_revision


def bump_show_data_revision(db: Session, show: Show) -> int:
    show.show_data_revision += 1
    db.flush()
    return show.show_data_revision
