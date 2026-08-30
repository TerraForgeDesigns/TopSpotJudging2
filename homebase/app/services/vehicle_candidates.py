"""
Recording New Vehicle Names candidates from sync — see PROTOCOL.md's
`make_manually_entered`/`model_manually_entered` fields and CONTEXT.md/
DECISIONS.md's SPEC-B reference. The full review/approval workflow (the
queue UI, merging a candidate into an approved VehicleModel, computing
`vehicle_additions` for the sync response) is HB5's job — this module
only does the one thing that has to happen at sync time, before HB5
exists: capture that a judge typed a make/model combination rather than
picking one from the approved list, so there's something for that future
review queue to work from.
"""
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.models import Car, Show, VehicleCandidate, VehicleCandidateSighting


def _normalized_key(make: str, model: str) -> str:
    return f"{make.strip().lower()}|{model.strip().lower()}"


def record_sighting(db: Session, show: Show, car: Car, make: str | None, model: str | None) -> None:
    """Called whenever a submission arrives with make_manually_entered or
    model_manually_entered set — see services/sync.py. A candidate needs
    both a make and a model to mean anything; if either is blank there's
    nothing to record."""
    if not make or not model:
        return

    key = _normalized_key(make, model)
    now = datetime.now(timezone.utc)
    candidate = db.scalars(select(VehicleCandidate).where(VehicleCandidate.normalized_key == key)).first()
    if candidate is None:
        candidate = VehicleCandidate(
            make_name=make.strip(),
            model_name=model.strip(),
            normalized_key=key,
            times_seen=0,
            first_seen_at=now,
            last_seen_at=now,
        )
        db.add(candidate)
        db.flush()  # need candidate.id for the sighting below

    candidate.times_seen += 1
    candidate.last_seen_at = now
    db.add(VehicleCandidateSighting(candidate_id=candidate.id, entry_id=car.id, show_id=show.id, seen_at=now))
