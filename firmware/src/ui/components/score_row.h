// A single row of numbered tap targets, one per possible score — for the
// show's 1-5 or 1-10 ranges (see services/range_escalation on the Home
// Base side; the 1-25 range uses score_grid.h's 5x5 layout instead, since
// 25 targets in one row would be too narrow to hit reliably outdoors).
// Exactly one target is ever selected at a time (radio behavior) — there
// is no "no score" state once judging on a category has started; see
// CONTEXT.md's scoring section on why an unscored car is never a 0.
#pragma once

#include <lvgl.h>

namespace ui::components {

// Called with the newly selected score (1..rangeMax) whenever the judge
// taps a different value.
using ScoreCallback = void (*)(void* ctx, int value);

// `rangeMax` must be <= 10 — see score_grid.h for 25. `initialValue` is
// 0 for "nothing selected yet," or 1..rangeMax to preselect (e.g. showing
// a previously entered score when editing).
lv_obj_t* scoreRow(lv_obj_t* parent, int rangeMax, int initialValue, ScoreCallback cb, void* ctx);

}  // namespace ui::components
