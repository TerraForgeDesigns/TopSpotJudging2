"""
Award slots. A host defines what's being awarded (services here handle
CRUD); the winner is auto-suggested from the relevant ranking in
services/scoring.py and stored in Award.winner_car_id only once the host
confirms or overrides it — that column is the single source of truth for
what actually gets announced.
"""
from dataclasses import dataclass, field

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import Award, AwardCategory, Car, JudgingScore, JudgingSubmission, PhotoType
from app.services.photo_ingest import get_car_photo
from app.services.scoring import (
    RankedEntry,
    class_rankings,
    criteria_leaderboards,
    get_accepted_submission,
    overall_rankings,
    submission_total,
)


def list_awards(db: Session, show_id: int) -> list[Award]:
    return list(
        db.scalars(
            select(Award)
            .where(Award.show_id == show_id)
            .options(
                selectinload(Award.criteria),
                selectinload(Award.car_class),
                selectinload(Award.winner_car),
            )
            .order_by(Award.sort_order)
        )
    )


def get_award(db: Session, award_id: int) -> Award | None:
    return db.get(Award, award_id)


def create_award(
    db: Session,
    show_id: int,
    name: str,
    category: AwardCategory,
    criteria_id: int | None = None,
    class_id: int | None = None,
) -> Award:
    max_order = db.scalar(select(Award.sort_order).where(Award.show_id == show_id).order_by(Award.sort_order.desc()))
    award = Award(
        show_id=show_id,
        name=name.strip(),
        category=category,
        criteria_id=criteria_id if category == AwardCategory.CRITERIA else None,
        class_id=class_id if category == AwardCategory.CLASS else None,
        sort_order=(max_order or 0) + 1,
    )
    db.add(award)
    db.commit()
    db.refresh(award)
    return award


def reorder_awards(db: Session, show_id: int, ordered_award_ids: list[int]) -> None:
    """Persists the host's drag-to-reorder sequence for the presentation
    walk order. Only touches awards that belong to this show — an id from
    a stale page load for a different show is silently ignored rather than
    trusted."""
    valid_ids = {a.id for a in db.scalars(select(Award).where(Award.show_id == show_id))}
    for index, award_id in enumerate(ordered_award_ids):
        if award_id in valid_ids:
            award = db.get(Award, award_id)
            award.sort_order = index
    db.commit()


def delete_award(db: Session, award_id: int) -> None:
    award = db.get(Award, award_id)
    if award is None:
        raise ValueError(f"No award with id {award_id}")
    db.delete(award)
    db.commit()


def set_winner(db: Session, award_id: int, car_id: int | None) -> Award:
    """car_id=None clears the assignment (host wants to go back to the
    auto-suggestion, or hasn't decided yet)."""
    award = db.get(Award, award_id)
    if award is None:
        raise ValueError(f"No award with id {award_id}")
    award.winner_car_id = car_id
    db.commit()
    db.refresh(award)
    return award


@dataclass
class AwardSuggestion:
    entry: RankedEntry | None  # None if there's nothing scored yet to suggest from
    ambiguous: bool  # True if rank 1 is a tie — no unambiguous suggestion


def suggest_winner(db: Session, award: Award) -> AwardSuggestion:
    """Rank-1 car from the relevant leaderboard, or an "ambiguous" flag if
    rank 1 is itself a tie — ties never get silently auto-picked."""
    if award.category == AwardCategory.OVERALL:
        ranking = overall_rankings(db, award.show_id)
    elif award.category == AwardCategory.CRITERIA:
        boards = criteria_leaderboards(db, award.show_id)
        ranking = next((entries for criteria, entries in boards if criteria.id == award.criteria_id), [])
    elif award.category == AwardCategory.CLASS:
        groups = class_rankings(db, award.show_id)
        ranking = next((entries for car_class, entries in groups if car_class and car_class.id == award.class_id), [])
    else:
        ranking = []

    leaders = [entry for entry in ranking if entry.rank == 1]
    if not leaders:
        return AwardSuggestion(entry=None, ambiguous=False)
    if len(leaders) > 1:
        return AwardSuggestion(entry=None, ambiguous=True)
    return AwardSuggestion(entry=leaders[0], ambiguous=False)


@dataclass
class PresentationSlide:
    """Everything the presentation screen needs for one award, pre-resolved
    server-side so the page is a single load with no follow-up fetches —
    see DESIGN.md awards-presentation-mode: no loading spinners visible to
    the audience."""

    award_id: int
    award_name: str
    car_id: int
    registration_number: str
    display_car_number: str
    year: int
    make: str
    model: str
    announcer_name: str | None
    total_score: int
    scores: list[tuple[str, int]] = field(default_factory=list)  # (criteria_name, points), in sort_order
    car_photo_url: str | None = None
    car_thumb_url: str | None = None
    judge_sheet_url: str | None = None


def build_presentation_sequence(db: Session, show_id: int) -> list[PresentationSlide]:
    """One slide per award that has a resolvable winner (an explicit
    override, or an unambiguous suggestion) — an award nobody's decided
    yet (nothing scored, or a tie the host hasn't broken) has nothing to
    show, so it's left out rather than presented broken. Order follows
    Award.sort_order, i.e. whatever the host arranged via drag-to-reorder."""
    awards = list_awards(db, show_id)
    slides: list[PresentationSlide] = []

    for award in awards:
        winner_id = award.winner_car_id
        if winner_id is None:
            suggestion = suggest_winner(db, award)
            winner_id = suggestion.entry.car.id if suggestion.entry else None
        if winner_id is None:
            continue

        car = db.scalars(
            select(Car)
            .where(Car.id == winner_id)
            .options(
                selectinload(Car.submissions)
                .selectinload(JudgingSubmission.scores)
                .selectinload(JudgingScore.criteria)
            )
        ).first()
        if car is None:
            continue

        submission = get_accepted_submission(car)
        total = submission_total(submission) if submission else 0
        scores: list[tuple[str, int]] = []
        if submission is not None:
            ordered = sorted(submission.scores, key=lambda s: s.criteria.sort_order)
            scores = [(s.criteria.name, s.points) for s in ordered]

        car_photo = get_car_photo(db, car.id, PhotoType.CAR)
        judge_sheet_photo = get_car_photo(db, car.id, PhotoType.JUDGE_SHEET)

        slides.append(
            PresentationSlide(
                award_id=award.id,
                award_name=award.name,
                car_id=car.id,
                registration_number=car.registration_number,
                display_car_number=car.display_car_number,
                year=car.year,
                make=car.make,
                model=car.model,
                announcer_name=car.announcer_name,
                total_score=total,
                scores=scores,
                car_photo_url=f"/photo-files/{car_photo.file_path}" if car_photo else None,
                car_thumb_url=(
                    f"/photo-files/{car_photo.thumbnail_path or car_photo.file_path}" if car_photo else None
                ),
                judge_sheet_url=f"/photo-files/{judge_sheet_photo.file_path}" if judge_sheet_photo else None,
            )
        )

    return slides
