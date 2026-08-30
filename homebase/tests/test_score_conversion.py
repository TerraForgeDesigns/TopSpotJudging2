"""
Score conversion — see CONTEXT.md's "Score conversion when the range
escalates" section. These fixtures are transcribed literally from that
document's verified tables; do not "simplify" or recompute them by hand.

services/score_conversion.py does not exist yet as of this commit — every
test here is expected to fail with a collection/import error until Step 5
of this task builds it. That's intentional (see the task's execution
order): these tests are written against the INTENDED behavior first.
"""
import pytest

from app.services.score_conversion import convert_score

# CONTEXT.md: 1-5 -> 1-10
TABLE_1_5_TO_1_10 = [
    (1, 2),
    (2, 4),
    (3, 6),
    (4, 8),
    (5, 10),
]

# CONTEXT.md: 1-5 -> 1-25
TABLE_1_5_TO_1_25 = [
    (1, 5),
    (2, 10),
    (3, 15),
    (4, 20),
    (5, 25),
]

# CONTEXT.md: 1-10 -> 1-25
TABLE_1_10_TO_1_25 = [
    (1, 3),
    (2, 5),
    (3, 8),
    (4, 10),
    (5, 13),
    (6, 15),
    (7, 18),
    (8, 20),
    (9, 23),
    (10, 25),
]


@pytest.mark.parametrize("original,expected", TABLE_1_5_TO_1_10)
def test_conversion_table_1_5_to_1_10(original, expected):
    assert convert_score(original, 5, 10) == expected


@pytest.mark.parametrize("original,expected", TABLE_1_5_TO_1_25)
def test_conversion_table_1_5_to_1_25(original, expected):
    assert convert_score(original, 5, 25) == expected


@pytest.mark.parametrize("original,expected", TABLE_1_10_TO_1_25)
def test_conversion_table_1_10_to_1_25(original, expected):
    assert convert_score(original, 10, 25) == expected


def test_round_half_up_not_banker_rounding():
    """1 * 25/10 = 2.5 exactly. ROUND_HALF_UP must produce 3. Python's
    built-in round() uses banker's rounding and would produce 2 — this
    test exists specifically to catch a switch back to round()."""
    assert convert_score(1, 10, 25) == 3
    assert round(2.5) == 2  # documents WHY convert_score can't use round()


@pytest.mark.parametrize(
    "original,expected",
    [(1, 3), (3, 8), (5, 13), (7, 18), (9, 23)],  # the five x.5 boundary cases in the 1-10 -> 1-25 table
)
def test_round_half_up_on_every_half_integer_boundary(original, expected):
    assert convert_score(original, 10, 25) == expected


def test_conversion_is_a_no_op_when_range_is_unchanged():
    assert convert_score(4, 10, 10) == 4


def test_conversion_is_pure_and_deterministic():
    """No hidden state — calling it twice with the same original inputs
    must give the same answer both times, regardless of what else has
    happened in between."""
    first = convert_score(7, 10, 25)
    second = convert_score(7, 10, 25)
    assert first == second == 18
