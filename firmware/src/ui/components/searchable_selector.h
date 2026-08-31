// A search field over a caller-owned list of strings (e.g. the approved
// vehicle make/model list — SPEC-B on the Home Base side) plus a filtered,
// BOUNDED result list. Never one LVGL object per source row: with a list
// that can run into the hundreds, that's the classic way to make a 4MB/
// 8MB-PSRAM device crawl or run out of object-pool memory. At most
// SEARCHABLE_SELECTOR_MAX_ROWS rows are ever created at once, no matter
// how many items match — past that, a plain hint tells the judge to keep
// typing to narrow it down, per this task's instruction.
#pragma once

#include <lvgl.h>

namespace ui::components {

constexpr int SEARCHABLE_SELECTOR_MAX_ROWS = 40;

using SelectorItemCallback = void (*)(void* ctx, const char* selectedText, int selectedIndex);

// `items` is NOT copied — it must stay valid for as long as the returned
// object exists (the caller owns the source list, typically a static
// table or something already loaded for the whole screen's lifetime).
lv_obj_t* searchableSelector(lv_obj_t* parent, const char** items, int itemCount, const char* placeholder,
                              SelectorItemCallback cb, void* ctx);

}  // namespace ui::components
