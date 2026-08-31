// 3x4 numeric keypad — digits 0-9 plus Clear and Delete, 74px keys (see
// components/button.h's BUTTON_H — same outdoor one-handed tap-target
// height used everywhere else). Used for entry-number entry and any other
// free-form number input; not used for scoring — see score_row.h/
// score_grid.h for that, which are tap-once selectors, not typed numbers.
#pragma once

#include <lvgl.h>

namespace ui::components {

// Called on every key press. `key` is '0'-'9' for digits, 'C' for Clear
// (wipes the whole value), 'B' for Backspace (deletes the last digit).
// The keypad itself holds no numeric state — the caller's textarea/label
// does; this just reports which key was tapped.
using KeypadCallback = void (*)(void* ctx, char key);

lv_obj_t* numericKeypad(lv_obj_t* parent, KeypadCallback cb, void* ctx);

}  // namespace ui::components
