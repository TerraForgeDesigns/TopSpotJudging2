"""
The Create Show wizard's business logic — see CONTEXT.md's "Show setup"
section. The web layer (app/web/show_wizard.py) is a thin wrapper: it
parses form fields, calls into this module, and renders templates. Every
validation rule and every piece of "what does this step mean" logic
lives here so it's testable without going through HTTP.

Wizard state lives entirely in one ShowDraft row's `data` JSON blob (see
models/show_draft.py) — every function here reads the whole dict and
returns/assigns a NEW dict rather than mutating in place, because plain
JSON columns don't auto-track in-place dict mutation in SQLAlchemy; a
fresh dict assigned to `draft.data` is what actually marks the row dirty.
"""
from dataclasses import dataclass, field
from datetime import date

from sqlalchemy.orm import Session

from app.models import Award, Car, JudgingCategory, Show, ShowDraft
from app.models.enums import AwardRankingBasis
from app.services.range_escalation import TIER_THRESHOLDS, range_max_for_car_count
from app.services.shows import set_active_show

# The five built-in Judging Categories, in CONTEXT.md's listed order —
# that order is also the default tie-break priority order (Step 3: "Default
# to display order"). `id` is a stable key used throughout the draft and
# is NOT a database id (no JudgingCategory rows exist until materialize()).
BUILTIN_CATEGORIES = [
    {"id": "engine", "name": "Engine"},
    {"id": "exterior", "name": "Exterior"},
    {"id": "interior", "name": "Interior"},
    {"id": "paint", "name": "Paint"},
    {"id": "wheels_tires", "name": "Wheels / Tires"},
]

# The seven built-in Show Awards — see CONTEXT.md's Awards section for
# the ranking_basis mapping (own-category for Paint/Interior/Engine,
# Total for the rest).
BUILTIN_AWARDS = [
    {"name": "Best Paint", "ranking_basis": "paint"},
    {"name": "Best Interior", "ranking_basis": "interior"},
    {"name": "Best Engine", "ranking_basis": "engine"},
    {"name": "Best Car", "ranking_basis": "total"},
    {"name": "Best Truck", "ranking_basis": "total"},
    {"name": "Best Bike", "ranking_basis": "total"},
    {"name": "Best Rat Rod", "ranking_basis": "total"},
]

TOP_AWARD_OPTIONS = [10, 20, 50, 100, 150, 200]

# What the Add Award flow's "Which score should decide the winner?" offers
# — see CONTEXT.md. Order matches how it's listed there.
RANKING_BASIS_OPTIONS = [
    ("total", "Total Score"),
    ("engine", "Engine"),
    ("exterior", "Exterior"),
    ("interior", "Interior"),
    ("paint", "Paint"),
    ("wheels_tires", "Wheels / Tires"),
]
RANKING_BASIS_LABELS = dict(RANKING_BASIS_OPTIONS)


def new_draft_data() -> dict:
    categories = [
        {"id": c["id"], "name": c["name"], "active": True, "sort_order": i}
        for i, c in enumerate(BUILTIN_CATEGORIES)
    ]
    awards = [
        {
            "id": i + 1,
            "name": a["name"],
            "builtin": True,
            "active": True,
            "judge_chosen": True,
            "ranking_basis": a["ranking_basis"],
            "sort_order": i,
        }
        for i, a in enumerate(BUILTIN_AWARDS)
    ]
    return {
        "name": "",
        "event_date": "",
        "location": "",
        "notes": "",
        "car_count": None,
        "categories": categories,
        "overall_impression_enabled": False,
        "top_awards_count": None,
        "awards": awards,
        "next_award_id": len(awards) + 1,
    }


def create_draft(db: Session) -> ShowDraft:
    draft = ShowDraft(data=new_draft_data())
    db.add(draft)
    db.commit()
    db.refresh(draft)
    return draft


def get_draft(db: Session, draft_id: int) -> ShowDraft | None:
    return db.get(ShowDraft, draft_id)


def _save(db: Session, draft: ShowDraft, data: dict) -> None:
    draft.data = data
    db.commit()


# ---------------------------------------------------------------------
# Step 1 — Show Details
# ---------------------------------------------------------------------

def save_step1(db: Session, draft: ShowDraft, name: str, event_date: str, location: str, notes: str) -> dict[str, str]:
    """Returns a dict of field -> error message; empty means success (and
    the draft was saved). Nothing is saved if there's an error, per
    LANGUAGE.md: say which field and what to do."""
    errors: dict[str, str] = {}
    name = (name or "").strip()
    if not name:
        errors["name"] = "Show Name is required. Enter a name for the show before continuing."
    if not event_date:
        errors["event_date"] = "Show Date is required. Choose a date before continuing."
    elif not _is_valid_iso_date(event_date):
        errors["event_date"] = "Show Date doesn't look like a valid date. Choose a date before continuing."

    if errors:
        return errors

    data = dict(draft.data)
    data["name"] = name
    data["event_date"] = event_date
    data["location"] = (location or "").strip()
    data["notes"] = (notes or "").strip()
    _save(db, draft, data)
    return {}


def _is_valid_iso_date(value: str) -> bool:
    try:
        date.fromisoformat(value)
        return True
    except ValueError:
        return False


# ---------------------------------------------------------------------
# Step 2 — Number of Cars
# ---------------------------------------------------------------------

def save_step2(db: Session, draft: ShowDraft, car_count_raw: str) -> dict[str, str]:
    errors: dict[str, str] = {}
    car_count_raw = (car_count_raw or "").strip()
    car_count: int | None = None
    if not car_count_raw:
        errors["car_count"] = "How many cars will be in the show? Enter a number greater than zero before continuing."
    else:
        try:
            car_count = int(car_count_raw)
        except ValueError:
            errors["car_count"] = "Enter a whole number greater than zero before continuing."
        else:
            if car_count <= 0:
                errors["car_count"] = "Enter a number greater than zero before continuing."

    if errors:
        return errors

    data = dict(draft.data)
    data["car_count"] = car_count
    _save(db, draft, data)
    return {}


def near_threshold_nudge(car_count: int | None) -> str | None:
    """CONTEXT.md: cars judged at a lower range and later scaled up land
    on a coarse lattice (a car judged 1-5 and scaled to 1-25 can only
    score 5, 10, 15, 20, or 25) — so a host whose estimate is close to a
    tier threshold is nudged to enter their real expected total now,
    rather than discover the coarseness mid-show. See DECISIONS.md for
    the full reasoning behind why this is worth surfacing at all.

    "Within 10" = the 10 cars immediately at-or-below each threshold
    (e.g. 141-150 for the 150 threshold) — the range where an estimate
    that runs even slightly high would cross into the next tier."""
    if car_count is None:
        return None
    for threshold, _range_max in TIER_THRESHOLDS:
        if threshold - 10 < car_count <= threshold:
            return (
                f"If you expect more than {threshold} cars, enter your expected total now "
                "so scores do not need adjusting later."
            )
    return None


# ---------------------------------------------------------------------
# Step 3 — Judging Setup
# ---------------------------------------------------------------------

def max_score(data: dict) -> int:
    active_count = sum(1 for c in data["categories"] if c["active"])
    range_max = range_max_for_draft(data)
    return active_count * range_max


def range_max_for_draft(data: dict) -> int:
    car_count = data.get("car_count")
    return range_max_for_car_count(car_count) if car_count else 5


def active_categories_in_priority_order(data: dict) -> list[dict]:
    return sorted((c for c in data["categories"] if c["active"]), key=lambda c: c["sort_order"])


def toggle_category(db: Session, draft: ShowDraft, category_id: str) -> None:
    data = dict(draft.data)
    data["categories"] = [
        {**c, "active": not c["active"]} if c["id"] == category_id else dict(c) for c in data["categories"]
    ]
    _save(db, draft, data)


def rename_category(db: Session, draft: ShowDraft, category_id: str, name: str) -> None:
    name = (name or "").strip()
    if not name:
        return  # a blank rename is a no-op, not an error — the field just keeps its old value
    data = dict(draft.data)
    data["categories"] = [
        {**c, "name": name} if c["id"] == category_id else dict(c) for c in data["categories"]
    ]
    _save(db, draft, data)


def reorder_categories(db: Session, draft: ShowDraft, ordered_ids: list[str]) -> None:
    """ordered_ids is the new tie-break priority order for ACTIVE
    categories (from the drag list, which only shows active ones).
    Inactive categories keep their existing relative order, appended
    after — their sort_order still matters for display order if/when
    they're reactivated."""
    data = dict(draft.data)
    order_index = {cat_id: i for i, cat_id in enumerate(ordered_ids)}
    inactive = [c for c in data["categories"] if not c["active"]]
    active = [c for c in data["categories"] if c["active"]]
    active_sorted = sorted(active, key=lambda c: order_index.get(c["id"], len(ordered_ids)))
    reordered = active_sorted + inactive
    data["categories"] = [{**c, "sort_order": i} for i, c in enumerate(reordered)]
    _save(db, draft, data)


def toggle_overall_impression(db: Session, draft: ShowDraft) -> None:
    data = dict(draft.data)
    data["overall_impression_enabled"] = not data["overall_impression_enabled"]
    _save(db, draft, data)


def validate_step3(data: dict) -> str | None:
    if not any(c["active"] for c in data["categories"]):
        return "Choose at least one Judging Category before continuing."
    return None


# ---------------------------------------------------------------------
# Step 4 — Awards
# ---------------------------------------------------------------------

def set_top_awards_count(db: Session, draft: ShowDraft, count: int) -> None:
    data = dict(draft.data)
    data["top_awards_count"] = count
    _save(db, draft, data)


def active_awards_in_order(data: dict) -> list[dict]:
    return sorted((a for a in data["awards"] if a["active"]), key=lambda a: a["sort_order"])


def toggle_award(db: Session, draft: ShowDraft, award_id: int) -> None:
    data = dict(draft.data)
    data["awards"] = [
        {**a, "active": not a["active"]} if a["id"] == award_id else dict(a) for a in data["awards"]
    ]
    _save(db, draft, data)


def rename_award(db: Session, draft: ShowDraft, award_id: int, name: str) -> None:
    name = (name or "").strip()
    if not name:
        return
    data = dict(draft.data)
    data["awards"] = [{**a, "name": name} if a["id"] == award_id else dict(a) for a in data["awards"]]
    _save(db, draft, data)


def remove_award(db: Session, draft: ShowDraft, award_id: int) -> None:
    data = dict(draft.data)
    data["awards"] = [a for a in data["awards"] if a["id"] != award_id]
    _save(db, draft, data)


def reorder_awards(db: Session, draft: ShowDraft, ordered_ids: list[int]) -> None:
    data = dict(draft.data)
    order_index = {award_id: i for i, award_id in enumerate(ordered_ids)}
    data["awards"] = sorted(
        (dict(a) for a in data["awards"]), key=lambda a: order_index.get(a["id"], len(ordered_ids))
    )
    data["awards"] = [{**a, "sort_order": i} for i, a in enumerate(data["awards"])]
    _save(db, draft, data)


@dataclass
class AddAwardResult:
    errors: dict[str, str] = field(default_factory=dict)


def add_award(
    db: Session, draft: ShowDraft, name: str, judge_chosen: bool, ranking_basis: str | None
) -> AddAwardResult:
    errors: dict[str, str] = {}
    name = (name or "").strip()
    if not name:
        errors["name"] = "Award Name is required. Enter a name before adding the award."
    if judge_chosen and ranking_basis not in RANKING_BASIS_LABELS:
        errors["ranking_basis"] = "Choose which score should decide the winner before adding the award."
    if errors:
        return AddAwardResult(errors=errors)

    data = dict(draft.data)
    new_id = data["next_award_id"]
    data["awards"] = data["awards"] + [
        {
            "id": new_id,
            "name": name,
            "builtin": False,
            "active": True,
            "judge_chosen": judge_chosen,
            "ranking_basis": ranking_basis if judge_chosen else None,
            "sort_order": len(data["awards"]),
        }
    ]
    data["next_award_id"] = new_id + 1
    _save(db, draft, data)
    return AddAwardResult()


def validate_step4(data: dict) -> str | None:
    if data.get("top_awards_count") not in TOP_AWARD_OPTIONS:
        return "Choose how many top-scoring cars will receive awards before continuing."
    return None


# ---------------------------------------------------------------------
# Step 6 — Create Show
# ---------------------------------------------------------------------

def materialize(db: Session, draft: ShowDraft) -> Show:
    """Creates the real Show, its entries (numbered 001..N), its Judging
    Categories, and its Awards from the draft — see CONTEXT.md's
    "Show setup" and Step 6. Both revision counters start at 1 (the
    Show model's column defaults already do this). Deletes the draft on
    success — it has no life beyond this."""
    data = draft.data
    car_count = data["car_count"]
    range_max = range_max_for_car_count(car_count)

    show = Show(
        name=data["name"],
        event_date=date.fromisoformat(data["event_date"]),
        location=data["location"] or None,
        notes=data["notes"] or None,
        score_range_max=range_max,
        overall_impression_enabled=data["overall_impression_enabled"],
        top_awards_count=data["top_awards_count"],
    )
    db.add(show)
    db.flush()  # need show.id for the rows below

    for i in range(1, car_count + 1):
        db.add(
            Car(
                show_id=show.id,
                entry_number=f"{i:03d}",
                data_revision_at_change=show.show_data_revision,
            )
        )

    for category in data["categories"]:
        db.add(
            JudgingCategory(
                show_id=show.id,
                name=category["name"],
                active=category["active"],
                sort_order=category["sort_order"],
            )
        )

    for award in data["awards"]:
        db.add(
            Award(
                show_id=show.id,
                name=award["name"],
                active=award["active"],
                judge_chosen=award["judge_chosen"],
                ranking_basis=AwardRankingBasis(award["ranking_basis"]) if award["ranking_basis"] else None,
                sort_order=award["sort_order"],
            )
        )

    db.commit()
    db.refresh(show)

    set_active_show(db, show.id)

    db.delete(draft)
    db.commit()

    return show
