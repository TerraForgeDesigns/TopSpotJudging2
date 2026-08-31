#include "text_keyboard.h"

#include <cstring>

#include "../fonts/fonts.h"
#include "../theme.h"
#include "button.h"

namespace ui::components {

namespace {

// Custom, fully plain-ASCII keyboard maps — deliberately NOT lv_keyboard's
// stock maps, which label their Backspace/Enter/Shift/Close keys with
// LV_SYMBOL_* icon glyphs. Those live in a font codepoint range this
// project's embedded fonts don't cover (see fonts/fonts.h's ASCII-only
// decision) — using the stock maps as-is would render blank boxes for
// those keys. Lowercase letters only, no Shift row: LANGUAGE.md asks for
// the handheld's wording to be even simpler than Home Base's, and a judge
// typing a name or a search term outdoors doesn't need case-sensitivity
// for anything this firmware does with that text.
const char* TEXT_MAP[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "z", "x", "c", "v", "b", "n", "m", "\n",
    "Space", "Delete", "",
};

const char* NUMBER_MAP[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    ".", "0", "Delete", "",
};

// lv_keyboard's own auto-insert-into-textarea wiring (lv_keyboard_set_
// textarea) recognizes special keys only by matching their button text
// against LV_SYMBOL_* strings — which this keyboard's plain-text "Space"/
// "Delete" keys don't use, so that wiring is deliberately NOT used here.
// Every key press is handled explicitly below instead: fully correct
// regardless of what LVGL's internal string matching does or doesn't
// recognize, and never dependent on a glyph this font doesn't have.
void keyPressedCb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* kb = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* ta = static_cast<lv_obj_t*>(lv_event_get_user_data(e));

    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    const char* text = lv_btnmatrix_get_btn_text(kb, id);
    if (text == nullptr) return;

    if (strcmp(text, "Delete") == 0) {
        lv_textarea_del_char(ta);
    } else if (strcmp(text, "Space") == 0) {
        lv_textarea_add_char(ta, ' ');
    } else {
        lv_textarea_add_text(ta, text);
    }
}

struct OverlayState {
    lv_obj_t* overlay;
    lv_obj_t* textarea;
    char* initialText;  // heap copy — returned verbatim if Cancel is chosen
    TextInputCallback cb;
    void* userCtx;
};

void finish(OverlayState* state, bool accepted) {
    const char* text = accepted ? lv_textarea_get_text(state->textarea) : state->initialText;
    if (state->cb != nullptr) state->cb(state->userCtx, text, accepted);
    lv_obj_del(state->overlay);  // also frees state->initialText via the overlay's LV_EVENT_DELETE handler below
}

void overlayEventCb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    auto* state = static_cast<OverlayState*>(lv_event_get_user_data(e));
    delete[] state->initialText;
    delete state;
}

void doneClicked(lv_event_t* e) { finish(static_cast<OverlayState*>(lv_event_get_user_data(e)), true); }
void cancelClicked(lv_event_t* e) { finish(static_cast<OverlayState*>(lv_event_get_user_data(e)), false); }

lv_obj_t* buildOverlay(const char* initialText, const char* placeholder, uint32_t maxLen, const char** map,
                        TextInputCallback cb, void* ctx) {
    // Always built on the top layer, not the caller's `parent` — a modal
    // text entry needs to sit above the status bar and whichever screen
    // is currently active, and must survive independently of that
    // screen's own content tree (which screen_manager may tear down for
    // unrelated reasons while this is open, e.g. a stray back-navigation).
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_add_style(overlay, theme::screenBg(), 0);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    auto* state = new OverlayState{};
    state->overlay = overlay;
    size_t len = strlen(initialText != nullptr ? initialText : "");
    state->initialText = new char[len + 1];
    strcpy(state->initialText, initialText != nullptr ? initialText : "");
    state->cb = cb;
    state->userCtx = ctx;
    lv_obj_add_event_cb(overlay, overlayEventCb, LV_EVENT_DELETE, state);

    // Top row: textarea + Done + Cancel, 80px tall — well above the
    // 62-74px minimum tap-target band, since these are the two most
    // consequential taps on the whole screen.
    lv_obj_t* topRow = lv_obj_create(overlay);
    lv_obj_remove_style_all(topRow);
    lv_obj_set_size(topRow, LV_PCT(100), 80);
    lv_obj_set_style_pad_all(topRow, 8, 0);
    lv_obj_set_style_pad_column(topRow, 8, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ta = lv_textarea_create(topRow);
    lv_obj_add_style(ta, theme::raisedSurface(), 0);
    lv_obj_add_style(ta, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(ta, &ui_font_plex_400_16, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, initialText != nullptr ? initialText : "");
    if (placeholder != nullptr) lv_textarea_set_placeholder_text(ta, placeholder);
    if (maxLen > 0) lv_textarea_set_max_length(ta, maxLen);
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_set_height(ta, 64);
    state->textarea = ta;

    lv_obj_t* cancelBtn = secondaryButton(topRow, "Cancel");
    lv_obj_set_height(cancelBtn, 64);
    lv_obj_add_event_cb(cancelBtn, cancelClicked, LV_EVENT_CLICKED, state);

    lv_obj_t* doneBtn = primaryButton(topRow, "Done");
    lv_obj_set_height(doneBtn, 64);
    lv_obj_add_event_cb(doneBtn, doneClicked, LV_EVENT_CLICKED, state);

    lv_obj_t* kb = lv_keyboard_create(overlay);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, map, nullptr);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
    lv_obj_set_style_text_font(kb, &ui_font_archivo_700_23, LV_PART_ITEMS);
    lv_obj_add_style(kb, theme::raisedSurface(), LV_PART_ITEMS);
    lv_obj_add_style(kb, theme::btnPrimaryPressed(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_size(kb, LV_PCT(100), lv_disp_get_ver_res(nullptr) - 80);
    lv_obj_set_pos(kb, 0, 80);
    lv_obj_add_event_cb(kb, keyPressedCb, LV_EVENT_VALUE_CHANGED, ta);

    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    return overlay;
}

}  // namespace

lv_obj_t* textKeyboardOverlay(const char* initialText, const char* placeholder, uint32_t maxLen,
                               TextInputCallback cb, void* ctx) {
    return buildOverlay(initialText, placeholder, maxLen, TEXT_MAP, cb, ctx);
}

lv_obj_t* numericKeyboardOverlay(const char* initialText, const char* placeholder, uint32_t maxLen,
                                  TextInputCallback cb, void* ctx) {
    return buildOverlay(initialText, placeholder, maxLen, NUMBER_MAP, cb, ctx);
}

const char** plainTextKeyMap() { return TEXT_MAP; }

void keyPressedIntoTextarea(lv_event_t* e) { keyPressedCb(e); }

}  // namespace ui::components
