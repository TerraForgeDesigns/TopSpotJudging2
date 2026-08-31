#include "searchable_selector.h"

#include <cstdio>
#include <cstring>
#include <strings.h>  // strncasecmp

#include "../fonts/fonts.h"
#include "../theme.h"
#include "text_keyboard.h"

namespace ui::components {

namespace {

struct SelectorState {
    const char** items;
    int itemCount;
    lv_obj_t* searchField;
    lv_obj_t* resultList;
    lv_obj_t* hint;
    SelectorItemCallback cb;
    void* userCtx;
};

struct RowCtx {
    int itemIndex;
    SelectorState* state;
};

bool containsCaseInsensitive(const char* haystack, const char* needle) {
    if (needle[0] == '\0') return true;
    size_t needleLen = strlen(needle);
    for (const char* p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, needleLen) == 0) return true;
    }
    return false;
}

void rowEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    auto* rowCtx = static_cast<RowCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_DELETE) {
        delete rowCtx;
        return;
    }
    if (code != LV_EVENT_CLICKED) return;
    SelectorState* state = rowCtx->state;
    if (state->cb != nullptr) state->cb(state->userCtx, state->items[rowCtx->itemIndex], rowCtx->itemIndex);
}

void buildRow(lv_obj_t* parent, SelectorState* state, int itemIndex) {
    lv_obj_t* row = lv_btn_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, theme::raisedSurface(), 0);
    lv_obj_add_style(row, theme::btnPrimaryPressed(), LV_STATE_PRESSED);
    lv_obj_set_size(row, LV_PCT(100), 62);  // outdoor one-handed minimum tap height

    lv_obj_t* label = lv_label_create(row);
    lv_obj_add_style(label, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(label, &ui_font_plex_400_16, 0);
    lv_label_set_text(label, state->items[itemIndex]);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);

    // Freed via this row's own LV_EVENT_DELETE — gone the instant the
    // list is rebuilt on the next keystroke (see refilter()'s
    // lv_obj_clean(), which recursively deletes every row + fires this).
    auto* rowCtx = new RowCtx{itemIndex, state};
    lv_obj_add_event_cb(row, rowEventCb, LV_EVENT_ALL, rowCtx);
}

void refilter(SelectorState* state) {
    lv_obj_clean(state->resultList);
    const char* query = lv_textarea_get_text(state->searchField);

    int shown = 0;
    int matched = 0;
    for (int i = 0; i < state->itemCount; i++) {
        if (!containsCaseInsensitive(state->items[i], query)) continue;
        matched++;
        if (shown < SEARCHABLE_SELECTOR_MAX_ROWS) {
            buildRow(state->resultList, state, i);
            shown++;
        }
    }

    if (matched > shown) {
        char buf[64];
        // Plain hyphen, not an em-dash — the embedded fonts only cover
        // ASCII 0x20-0x7E (see fonts.h); anything outside that range
        // would render as a missing-glyph box, not a design choice.
        snprintf(buf, sizeof(buf), "%d more - keep typing to narrow it down", matched - shown);
        lv_label_set_text(state->hint, buf);
        lv_obj_clear_flag(state->hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(state->hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void searchTextAccepted(void* ctx, const char* text, bool accepted) {
    auto* state = static_cast<SelectorState*>(ctx);
    if (!accepted) return;
    lv_textarea_set_text(state->searchField, text);
    refilter(state);
}

void searchFieldClicked(lv_event_t* e) {
    auto* state = static_cast<SelectorState*>(lv_event_get_user_data(e));
    const char* current = lv_textarea_get_text(state->searchField);
    textKeyboardOverlay(current, "Search", 64, searchTextAccepted, state);
}

void ownerDeleteCb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    delete static_cast<SelectorState*>(lv_event_get_user_data(e));
}

}  // namespace

lv_obj_t* searchableSelector(lv_obj_t* parent, const char** items, int itemCount, const char* placeholder,
                              SelectorItemCallback cb, void* ctx) {
    auto* state = new SelectorState{items, itemCount, nullptr, nullptr, nullptr, cb, ctx};

    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_add_style(container, theme::screenBg(), 0);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 8, 0);
    lv_obj_add_event_cb(container, ownerDeleteCb, LV_EVENT_DELETE, state);

    lv_obj_t* search = lv_textarea_create(container);
    lv_obj_add_style(search, theme::raisedSurface(), 0);
    lv_obj_add_style(search, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(search, &ui_font_plex_400_16, 0);
    lv_textarea_set_one_line(search, true);
    if (placeholder != nullptr) lv_textarea_set_placeholder_text(search, placeholder);
    lv_obj_set_size(search, LV_PCT(100), 62);
    lv_obj_add_event_cb(search, searchFieldClicked, LV_EVENT_CLICKED, state);
    state->searchField = search;

    state->hint = lv_label_create(container);
    lv_obj_add_style(state->hint, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(state->hint, &ui_font_plex_400_14, 0);
    lv_obj_add_flag(state->hint, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* list = lv_obj_create(container);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_grow(list, 1);
    state->resultList = list;

    refilter(state);
    return container;
}

}  // namespace ui::components
