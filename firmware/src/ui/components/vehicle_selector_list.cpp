#include "vehicle_selector_list.h"

#include <cstdio>
#include <cstring>

#include "../fonts/fonts.h"
#include "../theme.h"
#include "list_row.h"
#include "text_keyboard.h"

namespace ui::components {

namespace {

constexpr int MAX_RECENTS_SHOWN = 10;

struct SelectorState {
    VehicleSelectorConfig config;
    lv_obj_t* searchArea;   // null when !showSearchBox
    lv_obj_t* scrollArea;
};

struct RowCtx {
    char name[VEHICLE_NAME_LEN];
    SelectorState* state;
    bool manual;  // true only for the single "Other / Enter Manually" row
};

void freeRowCtx(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    delete static_cast<RowCtx*>(lv_event_get_user_data(e));
}

void manualEntryAccepted(void* ctx, const char* text, bool accepted) {
    if (!accepted || text[0] == '\0') return;  // Cancel, or an empty Done — selector screen just stays put either way
    auto* state = static_cast<SelectorState*>(ctx);
    if (state->config.onChosen != nullptr) state->config.onChosen(state->config.callbackCtx, text, true);
}

void rowClicked(void* ctxRaw) {
    auto* ctx = static_cast<RowCtx*>(ctxRaw);
    if (ctx->manual) {
        // Pre-fills the manual-entry keyboard with whatever was already
        // typed in the search box — task item 6: "the judge must never
        // type the same thing twice."
        const char* prefill = (ctx->state->searchArea != nullptr) ? lv_textarea_get_text(ctx->state->searchArea) : "";
        textKeyboardOverlay(prefill, ctx->state->config.manualPlaceholder, VEHICLE_NAME_LEN - 1, manualEntryAccepted,
                             ctx->state);
        return;
    }
    if (ctx->state->config.onChosen != nullptr) {
        ctx->state->config.onChosen(ctx->state->config.callbackCtx, ctx->name, false);
    }
}

lv_obj_t* addRow(lv_obj_t* parent, SelectorState* state, const char* name, bool manual) {
    auto* ctx = new RowCtx{};
    strncpy(ctx->name, name, sizeof(ctx->name) - 1);
    ctx->state = state;
    ctx->manual = manual;
    // ctx is handed straight to listRow() as ITS OWN callback context
    // (listRow(parent, title, value, dot, cb, ctx) forwards `ctx` verbatim
    // into `cb(ctx)` on tap) — a second, independent LV_EVENT_DELETE
    // callback on the same row (below) frees it when the row is torn
    // down, since listRow() itself only owns and frees ITS OWN internal
    // wrapper struct, not whatever ctx the caller passed in.
    lv_obj_t* row = listRow(parent, manual ? "Other / Enter Manually" : name, nullptr, RowDot::None, rowClicked, ctx);
    lv_obj_add_event_cb(row, freeRowCtx, LV_EVENT_DELETE, ctx);
    return row;
}

void refilter(SelectorState* state) {
    lv_obj_clean(state->scrollArea);
    const char* query = (state->searchArea != nullptr) ? lv_textarea_get_text(state->searchArea) : "";

    if (query[0] == '\0' && state->config.recents != nullptr) {
        char recentBuf[MAX_RECENTS_SHOWN][VEHICLE_NAME_LEN];
        int recentCount = state->config.recents(state->config.recentsCtx, recentBuf, MAX_RECENTS_SHOWN);
        if (recentCount > 0) {
            lv_obj_t* header = lv_label_create(state->scrollArea);
            lv_obj_add_style(header, theme::textSecondary(), 0);
            lv_obj_set_style_text_font(header, &ui_font_plex_600_23, 0);
            lv_label_set_text(header, "Recently Used");
            for (int i = 0; i < recentCount; i++) addRow(state->scrollArea, state, recentBuf[i], false);
        }
    }

    addRow(state->scrollArea, state, "", true);  // Other / Enter Manually — always present, always right here

    char matchBuf[VEHICLE_SELECTOR_MAX_ROWS][VEHICLE_NAME_LEN];
    int matched = state->config.query(state->config.queryCtx, query, matchBuf, VEHICLE_SELECTOR_MAX_ROWS);
    int shown = matched < VEHICLE_SELECTOR_MAX_ROWS ? matched : VEHICLE_SELECTOR_MAX_ROWS;
    for (int i = 0; i < shown; i++) addRow(state->scrollArea, state, matchBuf[i], false);

    if (matched > shown) {
        lv_obj_t* hint = lv_label_create(state->scrollArea);
        lv_obj_add_style(hint, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(hint, &ui_font_plex_400_14, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d more - keep typing to narrow it down", matched - shown);
        lv_label_set_text(hint, buf);
    }
}

void refilterAfterKeypress(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    refilter(static_cast<SelectorState*>(lv_event_get_user_data(e)));
}

}  // namespace

void buildVehicleSelector(lv_obj_t* content, const VehicleSelectorConfig& config) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 8, 0);

    // `state` deliberately outlives this function via `content`'s own
    // LV_EVENT_DELETE — screen_manager tears the whole content tree down
    // on navigation (see screen_manager.cpp), which is exactly when this
    // should be freed too, same lifetime as the screen itself.
    auto* state = new SelectorState{config, nullptr, nullptr};
    lv_obj_add_event_cb(
        content, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_DELETE) delete static_cast<SelectorState*>(lv_event_get_user_data(e));
        },
        LV_EVENT_DELETE, state);

    if (config.showSearchBox) {
        lv_obj_t* search = lv_textarea_create(content);
        lv_obj_add_style(search, theme::raisedSurface(), 0);
        lv_obj_add_style(search, theme::textPrimary(), 0);
        lv_obj_set_style_text_font(search, &ui_font_plex_400_16, 0);
        lv_textarea_set_one_line(search, true);
        if (config.searchPlaceholder != nullptr) lv_textarea_set_placeholder_text(search, config.searchPlaceholder);
        lv_obj_set_size(search, LV_PCT(100), 62);
        state->searchArea = search;
    }

    lv_obj_t* scroll = lv_obj_create(content);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_size(scroll, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scroll, 6, 0);
    lv_obj_set_flex_grow(scroll, 1);
    state->scrollArea = scroll;

    if (config.showSearchBox) {
        // Inline, ALWAYS-ON-SCREEN keyboard, docked at the bottom of this
        // same screen — deliberately not text_keyboard.h's own pop-up
        // overlay, which would gate filtering behind a separate Done tap
        // instead of live-per-keystroke (task item 4.1).
        lv_obj_t* kb = lv_keyboard_create(content);
        lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, plainTextKeyMap(), nullptr);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
        lv_obj_set_style_text_font(kb, &ui_font_archivo_700_23, LV_PART_ITEMS);
        lv_obj_add_style(kb, theme::raisedSurface(), LV_PART_ITEMS);
        lv_obj_add_style(kb, theme::btnPrimaryPressed(), LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_set_size(kb, LV_PCT(100), 200);
        // Two independent callbacks on the same event, registration order
        // matters: text_keyboard.h's shared key-handler (user_data = the
        // TARGET TEXTAREA, matching its own contract) inserts/deletes the
        // pressed key first; only then does the second callback (user_data
        // = `state`) re-filter against the now-updated text.
        lv_obj_add_event_cb(kb, keyPressedIntoTextarea, LV_EVENT_VALUE_CHANGED, state->searchArea);
        lv_obj_add_event_cb(kb, refilterAfterKeypress, LV_EVENT_VALUE_CHANGED, state);
    }

    refilter(state);
}

}  // namespace ui::components
