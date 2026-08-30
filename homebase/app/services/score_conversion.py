"""
Score conversion when the scoring range escalates — see CONTEXT.md's
"Score conversion when the range escalates" section. Pure, stateless,
deterministic: takes the ORIGINAL score and ORIGINAL range explicitly
every time, so a caller can never accidentally compound rounding error
by converting from an already-converted value — see
services/range_escalation.py, which always reads original_points/
original_range_max off the stored row, never adjusted_points.

Uses decimal arithmetic with explicit ROUND_HALF_UP. Do NOT use Python's
built-in round() here — it uses banker's rounding (round-half-to-even)
and would turn 2.5 into 2 instead of 3, silently changing a judge's
original scoring intent. See tests/test_score_conversion.py, which
asserts this explicitly against CONTEXT.md's verified fixture tables.
"""
from decimal import ROUND_HALF_UP, Decimal


def convert_score(original_points: int, original_range_max: int, new_range_max: int) -> int:
    if original_range_max == new_range_max:
        return original_points
    ratio = Decimal(original_points) * Decimal(new_range_max) / Decimal(original_range_max)
    return int(ratio.quantize(Decimal("1"), rounding=ROUND_HALF_UP))
