from datetime import datetime, timezone

from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    pass


def utcnow() -> datetime:
    """Python-side timestamp (not SQL CURRENT_TIMESTAMP) so created_at/updated_at
    carry microsecond precision — delta sync (see PROTOCOL.md) compares these."""
    return datetime.now(timezone.utc)
