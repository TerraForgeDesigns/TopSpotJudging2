// A checkbox + label row — built on LVGL's lv_checkbox. Used for anything
// with an explicit yes/no completion state a judge sets by tapping (a
// nomination toggle, a "photo taken" acknowledgement, ...).
#pragma once

#include <lvgl.h>

namespace ui::components {

using ChecklistCallback = void (*)(void* ctx, bool checked);

lv_obj_t* checklistRow(lv_obj_t* parent, const char* label, bool initialChecked, ChecklistCallback cb, void* ctx);

}  // namespace ui::components
