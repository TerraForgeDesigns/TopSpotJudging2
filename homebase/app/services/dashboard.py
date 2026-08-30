"""
Dashboard stats and handheld sync status. Read-only queries — the
dashboard polls this every 15s (see web/pages.py), so keep it cheap.

DashboardSummary is deliberately its own type, not api/schemas.py's
ProtocolSummary — see Correction 2 / DECISIONS.md. Today they happen to
hold the same four fields, computed the same way; that's a coincidence,
not a contract. This one is free to grow (awards readiness, photo
status, judging progress — see CONTEXT.md's 8-section Show Dashboard,
HB2) without ever touching the wire contract.
"""
from dataclasses import dataclass
from datetime import datetime, timezone

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.models import Car, CarStatus, Handheld
from app.services.time_format import relative_time

# PROTOCOL.md: periodic sync defaults to every 3 min, backoff caps at 15 min.
# A handheld silent longer than the backoff ceiling is either off, out of
# range for good, or home base can't reach it — worth the host's attention.
FRESH_THRESHOLD_SECONDS = 3 * 60
STALE_THRESHOLD_SECONDS = 15 * 60


@dataclass
class DashboardSummary:
    total_cars: int
    judged: int
    unjudged: int
    flagged_conflict: int


def get_summary(db: Session, show_id: int) -> DashboardSummary:
    rows = db.execute(
        select(Car.status, func.count(Car.id)).where(Car.show_id == show_id).group_by(Car.status)
    ).all()
    counts = {status: count for status, count in rows}
    return DashboardSummary(
        total_cars=sum(counts.values()),
        judged=counts.get(CarStatus.JUDGED, 0),
        unjudged=counts.get(CarStatus.UNJUDGED, 0),
        flagged_conflict=counts.get(CarStatus.FLAGGED_CONFLICT, 0),
    )


@dataclass
class HandheldStatus:
    handheld: Handheld
    relative_sync: str
    battery_pct: int | None
    sync_state: str  # "fresh" | "stale" | "overdue"


def get_handheld_statuses(db: Session) -> list[HandheldStatus]:
    now = datetime.now(timezone.utc)
    handhelds = list(db.scalars(select(Handheld).order_by(Handheld.label)))

    statuses = []
    for hh in handhelds:
        if hh.last_sync_at is None:
            sync_state = "overdue"
        else:
            last_sync = hh.last_sync_at
            if last_sync.tzinfo is None:
                last_sync = last_sync.replace(tzinfo=timezone.utc)
            age = (now - last_sync).total_seconds()
            if age <= FRESH_THRESHOLD_SECONDS:
                sync_state = "fresh"
            elif age <= STALE_THRESHOLD_SECONDS:
                sync_state = "stale"
            else:
                sync_state = "overdue"

        statuses.append(
            HandheldStatus(
                handheld=hh,
                relative_sync=relative_time(hh.last_sync_at, now=now),
                battery_pct=hh.battery_pct,
                sync_state=sync_state,
            )
        )
    return statuses
