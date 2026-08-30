from datetime import datetime, timezone

from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    pass


def utcnow() -> datetime:
    """Python-side timestamp (not SQL CURRENT_TIMESTAMP) so created_at/
    submitted_at carry microsecond precision. NOT used for sync deltas —
    sync now compares two monotonically increasing revision counters
    (Show.configuration_revision / Show.show_data_revision), not
    timestamps, because handhelds have no battery-backed clock. See
    PROTOCOL.md and DECISIONS.md."""
    return datetime.now(timezone.utc)
