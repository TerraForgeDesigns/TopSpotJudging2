"""
services/results.py — Top Awards boundary-tie resolution. See CONTEXT.md
and this task's Results requirement: "4 cars are tied for the last 2
places in the Top 50. Choose which cars place."
"""
from app.services.results import compute_top_awards
from app.services.scoring import RankedEntry


def _entries(*specs):
    """specs: (car_id, rank) pairs — 'car' only needs a .id for this
    pure-function test, so a tiny stand-in avoids touching the DB."""
    class _Car:
        def __init__(self, id):
            self.id = id

    return [RankedEntry(rank=rank, car=_Car(car_id), key=()) for car_id, rank in specs]


def test_no_tie_at_boundary_is_immediately_resolved():
    ranked = _entries((1, 1), (2, 2), (3, 3), (4, 4), (5, 5))
    result = compute_top_awards(ranked, top_n=3)
    assert result.resolved is True
    assert [e.car.id for e in result.placed] == [1, 2, 3]
    assert result.tied_group == []


def test_tie_exactly_filling_remaining_slots_needs_no_decision():
    # Ranks 1, then two cars tied at rank 2 — top_n=3 has exactly enough
    # room for both, so there's no real choice to make.
    ranked = _entries((1, 1), (2, 2), (3, 2), (4, 4))
    result = compute_top_awards(ranked, top_n=3)
    assert result.resolved is True
    assert {e.car.id for e in result.placed} == {1, 2, 3}
    assert result.tied_group == []


def test_four_cars_tied_for_last_two_places_is_unresolved_without_a_choice():
    # 48 clear places, then 4 cars tied for places 49-50 (only 2 slots left).
    clear = [(i, i) for i in range(1, 49)]  # ranks 1..48, one car each
    tied = [(100, 49), (101, 49), (102, 49), (103, 49)]
    ranked = _entries(*clear, *tied)

    result = compute_top_awards(ranked, top_n=50)

    assert result.resolved is False
    assert len(result.placed) == 48
    assert result.slots_remaining == 2
    assert {e.car.id for e in result.tied_group} == {100, 101, 102, 103}


def test_boundary_tie_resolved_once_the_host_chooses():
    clear = [(i, i) for i in range(1, 49)]
    tied = [(100, 49), (101, 49), (102, 49), (103, 49)]
    ranked = _entries(*clear, *tied)

    result = compute_top_awards(ranked, top_n=50, resolved_car_ids={100, 102})

    assert result.resolved is True
    placed_ids = {e.car.id for e in result.placed}
    assert placed_ids == set(range(1, 49)) | {100, 102}
    assert len(result.placed) == 50


def test_partial_resolution_is_still_unresolved():
    """Choosing only 1 of the 2 needed slots must not be treated as done —
    an incomplete decision is not a decision."""
    tied = [(100, 1), (101, 1), (102, 1), (103, 1)]
    ranked = _entries(*tied)

    result = compute_top_awards(ranked, top_n=2, resolved_car_ids={100})

    assert result.resolved is False
    assert result.slots_remaining == 2


def test_top_n_greater_than_or_equal_to_field_size_returns_everyone():
    ranked = _entries((1, 1), (2, 2), (3, 2))
    result = compute_top_awards(ranked, top_n=10)
    assert result.resolved is True
    assert len(result.placed) == 3


def test_empty_ranking_is_trivially_resolved():
    result = compute_top_awards([], top_n=50)
    assert result.resolved is True
    assert result.placed == []
