"""
Dashboard stats and handheld sync status. Read-only queries — the
dashboard polls this every 15s (see web/pages.py), so keep it cheap.

DashboardSummary is deliberately its own type, not api/schemas.py's
ProtocolSummary — see Correction 2 / DECISIONS.md. Today they happen to
hold the same four core fields, computed the same way; that's a
coincidence, not a contract. This one is free to grow (it already has
two fields ProtocolSummary will never have — cars_missing_photos and
awards_needing_winner) without ever touching the wire contract.
"""
from dataclasses import dataclass
from datetime import datetime, timezone

from sqlalchemy import func, select
from sqlalchemy.orm import Session

from app.models import Award, Car, CarStatus, Handheld, Photo, PhotoStatus, PhotoType
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
    cars_missing_photos: int
    awards_needing_winner: int


def get_summary(db: Session, show_id: int) -> DashboardSummary:
    rows = db.execute(
        select(Car.status, func.count(Car.id)).where(Car.show_id == show_id).group_by(Car.status)
    ).all()
    counts = {status: count for status, count in rows}

    # A judged car needs both a MATCHED car photo and a MATCHED judge
    # sheet photo to be "done" — see CONTEXT.md's workflow. This counts
    # judged cars missing either one; it does not attempt to explain
    # DUPLICATE/UNMATCHED photos (see /photos for that).
    judged_car_ids = set(
        db.scalars(
            select(Car.id).where(Car.show_id == show_id, Car.status == CarStatus.JUDGED)
        )
    )
    cars_missing_photos = 0
    if judged_car_ids:
        cars_with_both_photos = set()
        car_photo_rows = db.execute(
            select(Photo.car_id, Photo.photo_type)
            .where(Photo.car_id.in_(judged_car_ids), Photo.status == PhotoStatus.MATCHED)
        ).all()
        by_car: dict[int, set] = {}
        for car_id, photo_type in car_photo_rows:
            by_car.setdefault(car_id, set()).add(photo_type)
        for car_id, types in by_car.items():
            if PhotoType.CAR in types and PhotoType.JUDGE_SHEET in types:
                cars_with_both_photos.add(car_id)
        cars_missing_photos = len(judged_car_ids - cars_with_both_photos)

    # Only organiser-chosen awards are counted — a judge-chosen award's
    # readiness depends on nomination + ranking logic this build doesn't
    # have yet (HB7), so it would be dishonest to report on it here.
    awards_needing_winner = (
        db.scalar(
            select(func.count(Award.id)).where(
                Award.show_id == show_id,
                Award.active.is_(True),
                Award.judge_chosen.is_(False),
                Award.winner_car_id.is_(None),
            )
        )
        or 0
    )

    return DashboardSummary(
        total_cars=sum(counts.values()),
        judged=counts.get(CarStatus.JUDGED, 0),
        unjudged=counts.get(CarStatus.UNJUDGED, 0),
        flagged_conflict=counts.get(CarStatus.FLAGGED_CONFLICT, 0),
        cars_missing_photos=cars_missing_photos,
        awards_needing_winner=awards_needing_winner,
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
