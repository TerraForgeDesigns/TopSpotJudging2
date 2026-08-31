// 5x5 grid of tap targets for the show's 1-25 score range — see
// score_row.h for the 1-5/1-10 single-row version. Same radio-select
// semantics: exactly one cell selected at a time, never "none."
#pragma once

#include <lvgl.h>

namespace ui::components {

using ScoreCallback = void (*)(void* ctx, int value);

// `initialValue` is 0 for nothing selected, or 1..25 to preselect.
lv_obj_t* scoreGrid(lv_obj_t* parent, int initialValue, ScoreCallback cb, void* ctx);

}  // namespace ui::components
