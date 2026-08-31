// A themed card surface (ink800, rounded, 1px border) — the base every
// other grouped-content component (checklist row groups, photo slot
// groups, ...) sits inside, matching DESIGN.md's card spec exactly.
#pragma once

#include <lvgl.h>

namespace ui::components {

lv_obj_t* card(lv_obj_t* parent);

}  // namespace ui::components
