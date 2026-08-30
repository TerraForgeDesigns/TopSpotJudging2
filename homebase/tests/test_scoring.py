"""
services/scoring.py's ranking kernel — see CONTEXT.md's Tie-breaking
section and DECISIONS.md's tie-break density reasoning. Two things this
task specifically asked for coverage on: an unscored car never ranking
below a genuinely low-scoring car (it's excluded entirely, not a phantom
zero), and the tie-break cascade broken at every stage — first category,
third category, Overall Impression, and a fully unbreakable tie.

Four active categories (not three) are used for the category-tie tests:
since a car's total is exactly the sum of its active category scores,
"same total + same category 1 + same category 2" arithmetically FORCES
category 3 to match too, in a 3-category show — there's no way to
isolate a break at the 3rd of 3 without a 4th category to absorb the
compensating difference while sitting at a LOWER tie-break priority than
the one actually being tested.
"""
from datetime import date, datetime, timezone

import pytest

from app.models import Car, CarStatus, Handheld, JudgingCategory, JudgingScore, JudgingSubmission, Show, SubmissionStatus
from app.services.scoring import overall_rankings


@pytest.fixture()
def handheld(db_session):
    hh = Handheld(label="hh-1")
    db_session.add(hh)
    db_session.commit()
    return hh


def _judge(db, show, handheld, car, category_points: dict, overall_impression=None, closed_at_uptime_ms=0):
    submission = JudgingSubmission(
        show_id=show.id, car_id=car.id, entry_number=car.entry_number, handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc), closed_at_uptime_ms=closed_at_uptime_ms, status=SubmissionStatus.ACCEPTED,
    )
    if overall_impression is not None:
        submission.overall_impression_original_points = overall_impression
        submission.overall_impression_original_range_max = show.score_range_max
        submission.overall_impression_adjusted_points = overall_impression
    db.add(submission)
    db.flush()
    for category, points in category_points.items():
        db.add(JudgingScore(submission_id=submission.id, category_id=category.id, original_points=points, original_range_max=show.score_range_max, adjusted_points=points))
    car.status = CarStatus.JUDGED
    db.commit()
    return submission


def _car(db, show, entry_number):
    car = Car(show_id=show.id, entry_number=entry_number, data_revision_at_change=1)
    db.add(car)
    db.commit()
    return car


# ---------------------------------------------------------------------
# Unscored cars are excluded, never a phantom low score
# ---------------------------------------------------------------------

def test_unscored_car_never_appears_below_a_low_scoring_car(db_session, handheld):
    show = Show(name="Show", event_date=date(2026, 9, 1), score_range_max=5)
    db_session.add(show)
    db_session.commit()
    engine = JudgingCategory(show_id=show.id, name="Engine", built_in_key="engine", sort_order=0, active=True)
    db_session.add(engine)
    db_session.commit()

    low_scorer = Car(show_id=show.id, entry_number="001", data_revision_at_change=1)
    unscored = Car(show_id=show.id, entry_number="002", data_revision_at_change=1)
    db_session.add_all([low_scorer, unscored])
    db_session.commit()
    _judge(db_session, show, handheld, low_scorer, {engine: 1})  # the worst possible real score

    ranked = overall_rankings(db_session, show.id)

    ranked_car_ids = [entry.car.id for entry in ranked]
    assert low_scorer.id in ranked_car_ids
    assert unscored.id not in ranked_car_ids  # excluded entirely, not ranked last
    assert len(ranked) == 1


# ---------------------------------------------------------------------
# Tie-break cascade — constructed ties broken at each stage
# ---------------------------------------------------------------------

@pytest.fixture()
def show_with_four_categories(db_session):
    show = Show(name="Show", event_date=date(2026, 9, 1), score_range_max=25, overall_impression_enabled=True)
    db_session.add(show)
    db_session.commit()
    a = JudgingCategory(show_id=show.id, name="A", built_in_key="engine", sort_order=0, active=True)
    b = JudgingCategory(show_id=show.id, name="B", built_in_key="exterior", sort_order=1, active=True)
    c = JudgingCategory(show_id=show.id, name="C", built_in_key="interior", sort_order=2, active=True)
    d = JudgingCategory(show_id=show.id, name="D", built_in_key="paint", sort_order=3, active=True)
    db_session.add_all([a, b, c, d])
    db_session.commit()
    return show, a, b, c, d


def test_tie_broken_at_first_category(db_session, handheld, show_with_four_categories):
    show, a, b, c, d = show_with_four_categories
    car1, car2 = _car(db_session, show, "001"), _car(db_session, show, "002")
    # Same total (45): car1 leads in A (1st priority), car2 compensates in D (4th) — irrelevant, since
    # tuple comparison resolves at A, the first point of difference.
    _judge(db_session, show, handheld, car1, {a: 15, b: 10, c: 10, d: 10}, overall_impression=20)
    _judge(db_session, show, handheld, car2, {a: 10, b: 10, c: 10, d: 15}, overall_impression=20, closed_at_uptime_ms=1)

    ranked = {e.car.id: e for e in overall_rankings(db_session, show.id)}
    assert ranked[car1.id].rank == 1
    assert ranked[car2.id].rank == 2
    assert ranked[car1.id].tied is False
    assert ranked[car2.id].tied is False


def test_tie_broken_at_third_category(db_session, handheld, show_with_four_categories):
    show, a, b, c, d = show_with_four_categories
    car1, car2 = _car(db_session, show, "001"), _car(db_session, show, "002")
    # Same total (45), same A, same B — car1 leads in C (3rd priority), car2 compensates in D (4th,
    # never reached since C already resolves it).
    _judge(db_session, show, handheld, car1, {a: 10, b: 10, c: 15, d: 10}, overall_impression=20)
    _judge(db_session, show, handheld, car2, {a: 10, b: 10, c: 10, d: 15}, overall_impression=20, closed_at_uptime_ms=1)

    ranked = {e.car.id: e for e in overall_rankings(db_session, show.id)}
    assert ranked[car1.id].rank == 1
    assert ranked[car2.id].rank == 2
    assert ranked[car1.id].tied is False


def test_tie_broken_by_overall_impression(db_session, handheld, show_with_four_categories):
    show, a, b, c, d = show_with_four_categories
    car1, car2 = _car(db_session, show, "001"), _car(db_session, show, "002")
    # Identical total AND identical every category score — only Overall Impression differs.
    same_scores = {a: 10, b: 10, c: 10, d: 10}
    _judge(db_session, show, handheld, car1, dict(same_scores), overall_impression=20)
    _judge(db_session, show, handheld, car2, dict(same_scores), overall_impression=15, closed_at_uptime_ms=1)

    ranked = {e.car.id: e for e in overall_rankings(db_session, show.id)}
    assert ranked[car1.id].rank == 1
    assert ranked[car2.id].rank == 2
    assert ranked[car1.id].tied is False


def test_tie_fully_unbreakable(db_session, handheld, show_with_four_categories):
    show, a, b, c, d = show_with_four_categories
    car1, car2 = _car(db_session, show, "001"), _car(db_session, show, "002")
    # Total, Overall Impression, and every category identical — nothing left to break the tie on.
    same_scores = {a: 10, b: 10, c: 10, d: 10}
    _judge(db_session, show, handheld, car1, dict(same_scores), overall_impression=20)
    _judge(db_session, show, handheld, car2, dict(same_scores), overall_impression=20, closed_at_uptime_ms=1)

    ranked = {e.car.id: e for e in overall_rankings(db_session, show.id)}
    assert ranked[car1.id].rank == 1
    assert ranked[car2.id].rank == 1  # SAME rank — a genuine, unresolved tie
    assert ranked[car1.id].tied is True
    assert ranked[car2.id].tied is True
