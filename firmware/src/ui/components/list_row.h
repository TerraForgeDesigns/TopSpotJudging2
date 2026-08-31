// A tappable row: title left, an optional secondary/value string right,
// optional status dot. Used for anything list-shaped that isn't a search
// result (see searchable_selector.h for that) — e.g. a menu of debug demo
// screens, a handheld list, a settings row.
#pragma once

#include <lvgl.h>

namespace ui::components {

enum class RowDot { None, Good, Pending, Bad };

using ListRowCallback = void (*)(void* ctx);

// `value` and dot are optional (nullptr / RowDot::None) — a plain
// navigation row has neither.
lv_obj_t* listRow(lv_obj_t* parent, const char* title, const char* value, RowDot dot, ListRowCallback cb, void* ctx);

}  // namespace ui::components
