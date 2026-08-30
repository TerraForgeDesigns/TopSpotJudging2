"""
Award winner resolution — see CONTEXT.md's Awards section. Distinct from
services/awards_setup.py (which manages award SLOTS — add/rename/reorder/
toggle/who-picks) — this module is entirely about deciding and recording
WHO WON, which is HB7's job and was deliberately left out of
awards_setup.py's scope.

Judge-chosen awards: the winner is the highest-ranked NOMINATED car by
the award's ranking_basis, using CONTEXT.md's award tie cascade (basis ->
Overall Impression if enabled -> Total score, when it wasn't already the
basis -> organiser decides). Nothing is auto-applied — see
suggest_award_winner() and choose_winner(): a suggestion is computed live
and shown, but Award.winner_car_id (the actual decision) is only ever set
by an explicit host action, same principle as the pre-Aug-2026 awards
model this replaced (see DECISIONS.md) — once set, it doesn't quietly
change if new scores come in later.

A ranking_basis of ENGINE/EXTERIOR/INTERIOR/PAINT/WHEELS_TIRES is matched
to a real JudgingCategory via built_in_key, NOT by name — see
models/category.py's docstring for why matching by name would silently
break the moment a category is renamed.
"""
from dataclasses import dataclass, field

from sqlalchemy import select
from sqlalchemy.orm import Session, selectinload

from app.models import (
    Award,
    AwardNomination,
    Car,
    Handheld,
    JudgingCategory,
    JudgingSubmission,
    Show,
    SubmissionStatus,
)
from app.models.enums import AwardRankingBasis
from app.services.revisions import bump_configuration_revision, bump_show_data_revision
from app.services.scoring import RankedEntry, entries_at_rank, list_scored_cars, rank_by_tuple, submission_total


def _category_by_built_in_key(db: Session, show_id: int) -> dict[str, JudgingCategory]:
    categories = db.scalars(select(JudgingCategory).where(JudgingCategory.show_id == show_id))
    return {c.built_in_key: c for c in categories if c.built_in_key}


def _basis_score(
    submission: JudgingSubmission, basis: AwardRankingBasis, category_by_key: dict[str, JudgingCategory]
) -> int | None:
    """None means this basis can't be evaluated for this submission right
    now (e.g. the category it needs doesn't exist / has no built_in_key
    on this show) — the caller must skip, never treat as 0."""
    if basis == AwardRankingBasis.TOTAL:
        return submission_total(submission)
    category = category_by_key.get(basis.value)
    if category is None:
        return None
    return next((s.adjusted_points for s in submission.scores if s.category_id == category.id), None)


def build_award_ranking_key(
    show: Show, submission: JudgingSubmission, basis: AwardRankingBasis, category_by_key: dict[str, JudgingCategory]
) -> tuple | None:
    """The award tie cascade from CONTEXT.md: the basis score, then
    Overall Impression (if enabled), then Total (only when the basis
    ISN'T already Total — no point comparing a value against itself)."""
    basis_score = _basis_score(submission, basis, category_by_key)
    if basis_score is None:
        return None
    key: list[int] = [basis_score]
    if show.overall_impression_enabled:
        key.append(submission.overall_impression_adjusted_points or 0)
    if basis != AwardRankingBasis.TOTAL:
        key.append(submission_total(submission))
    return tuple(key)


def award_rankings(db: Session, show: Show, award: Award) -> list[RankedEntry]:
    """Nominated cars only, ranked by the award's own cascade. Empty for
    an organiser-chosen award (judge_chosen=False) — there's nothing to
    rank, the organiser picks directly."""
    if not award.judge_chosen or award.ranking_basis is None:
        return []
    category_by_key = _category_by_built_in_key(db, show.id)
    nominations = list(
        db.scalars(
            select(AwardNomination)
            .where(AwardNomination.award_id == award.id)
            .options(selectinload(AwardNomination.submission).selectinload(JudgingSubmission.scores))
        )
    )
    entries: list[tuple[Car, tuple]] = []
    seen_car_ids: set[int] = set()
    for nomination in nominations:
        submission = nomination.submission
        # Only the car's CURRENT accepted submission's nomination counts —
        # a nomination on a submission a conflict resolution later
        # rejected doesn't represent the car's actual judging anymore.
        if submission.status != SubmissionStatus.ACCEPTED:
            continue
        if submission.car_id in seen_car_ids:
            continue
        key = build_award_ranking_key(show, submission, award.ranking_basis, category_by_key)
        if key is None:
            continue
        seen_car_ids.add(submission.car_id)
        entries.append((submission.car, key))
    return rank_by_tuple(entries)


@dataclass
class AwardSuggestion:
    entry: RankedEntry | None  # None if there's nothing to suggest from yet
    ambiguous: bool  # True if rank 1 is itself a tie — no unambiguous suggestion


def suggest_award_winner(db: Session, show: Show, award: Award) -> AwardSuggestion:
    ranking = award_rankings(db, show, award)
    leaders = entries_at_rank(ranking, 1) if ranking else []
    if not leaders:
        return AwardSuggestion(entry=None, ambiguous=False)
    if len(leaders) > 1:
        return AwardSuggestion(entry=None, ambiguous=True)
    return AwardSuggestion(entry=leaders[0], ambiguous=False)


def high_scorers_not_nominated(db: Session, show: Show, award: Award) -> list[tuple[Car, int]]:
    """Cars that scored at least as well (by the award's basis) as the
    best NOMINATED car, but were never nominated. Never auto-included,
    never hidden — CONTEXT.md/this task: the organiser decides. If
    nobody was nominated at all, compares against the best score among
    ALL judged cars instead, so a strong field with zero nominations
    still gets flagged rather than silently producing an empty list."""
    if not award.judge_chosen or award.ranking_basis is None:
        return []

    category_by_key = _category_by_built_in_key(db, show.id)
    ranking = award_rankings(db, show, award)
    nominated_ids = {entry.car.id for entry in ranking}

    scored_with_basis: list[tuple[Car, int]] = []
    for car, submission in list_scored_cars(db, show.id):
        score = _basis_score(submission, award.ranking_basis, category_by_key)
        if score is not None:
            scored_with_basis.append((car, score))

    if ranking:
        threshold = ranking[0].key[0]  # best basis score among nominees
    elif scored_with_basis:
        threshold = max(score for _, score in scored_with_basis)
    else:
        return []

    result = [(car, score) for car, score in scored_with_basis if car.id not in nominated_ids and score >= threshold]
    return sorted(result, key=lambda pair: -pair[1])


@dataclass
class JudgeNominationStats:
    handheld_label: str
    cars_judged: int
    nominations_made: int
    rate: float  # nominations per car judged — the "how generous is this judge" signal


def nominations_per_judge_report(db: Session, show_id: int) -> list[JudgeNominationStats]:
    """Judges nominate at very different rates — a generous judge gives
    their cars more chances at a Show Award than a conservative one.
    Surfaced so the organiser can catch it, not to correct it
    automatically (there's no "correct" rate)."""
    submissions = list(
        db.scalars(
            select(JudgingSubmission)
            .where(JudgingSubmission.show_id == show_id, JudgingSubmission.status == SubmissionStatus.ACCEPTED)
            .options(selectinload(JudgingSubmission.handheld), selectinload(JudgingSubmission.nominations))
        )
    )
    by_handheld: dict[int, list[JudgingSubmission]] = {}
    for submission in submissions:
        by_handheld.setdefault(submission.handheld_id, []).append(submission)

    stats = []
    for handheld_id, subs in by_handheld.items():
        cars_judged = len(subs)
        nominations_made = sum(len(s.nominations) for s in subs)
        stats.append(
            JudgeNominationStats(
                handheld_label=subs[0].handheld.label,
                cars_judged=cars_judged,
                nominations_made=nominations_made,
                rate=(nominations_made / cars_judged) if cars_judged else 0.0,
            )
        )
    return sorted(stats, key=lambda s: -s.rate)


def choose_winner(db: Session, show: Show, award: Award, car_id: int | None) -> None:
    """car_id=None clears the decision (host wants to reconsider). Setting
    a winner un-marks "not presented" — the two are mutually exclusive
    outcomes for the same award."""
    award.winner_car_id = car_id
    if car_id is not None:
        award.marked_not_presented = False
    bump_configuration_revision(db, show)
    db.commit()


def use_suggested_winner(db: Session, show: Show, award: Award) -> str | None:
    """Accepts the live suggestion as the confirmed winner. Returns an
    error message (LANGUAGE.md style: what happened, what to do) if
    there's no unambiguous suggestion to accept — the host must use
    choose_winner() directly in that case."""
    suggestion = suggest_award_winner(db, show, award)
    if suggestion.entry is None:
        return "No cars have been nominated for this award yet. Choose a winner directly, or mark it as not presented."
    if suggestion.ambiguous:
        return "The nominated cars are tied. Choose which one wins."
    choose_winner(db, show, award, suggestion.entry.car.id)
    return None


def mark_not_presented(db: Session, show: Show, award: Award, flag: bool) -> None:
    award.marked_not_presented = flag
    if flag:
        award.winner_car_id = None
    bump_configuration_revision(db, show)
    db.commit()


def set_announcer_name(db: Session, show: Show, car: Car, name: str) -> None:
    car.announcer_name = name.strip() or None
    bump_show_data_revision(db, show)
    car.data_revision_at_change = show.show_data_revision
    db.commit()
