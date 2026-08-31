// Primary (gold, dark text) and secondary (outlined) buttons — 74px tall,
// the outdoor one-handed tap-target height DESIGN.md/this task specify.
// Both pull their colors from theme.h's shared styles, never a literal
// color, so theme::setMode() re-themes every button on screen instantly.
#pragma once

#include <lvgl.h>

namespace ui::components {

constexpr lv_coord_t BUTTON_H = 74;

// width: LV_SIZE_CONTENT-friendly value or LV_PCT(...)/an explicit pixel
// width — pass LV_SIZE_CONTENT for a label-sized button with internal
// padding, which is the common case for anything not spanning the screen.
lv_obj_t* primaryButton(lv_obj_t* parent, const char* label, lv_coord_t width = LV_SIZE_CONTENT);
lv_obj_t* secondaryButton(lv_obj_t* parent, const char* label, lv_coord_t width = LV_SIZE_CONTENT);

// Disabled state — grays out and blocks input without changing which
// style object it's built from (still re-themes correctly).
void setEnabled(lv_obj_t* button, bool enabled);

}  // namespace ui::components
