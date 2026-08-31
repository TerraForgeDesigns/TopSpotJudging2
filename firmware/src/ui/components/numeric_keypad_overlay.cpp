#include "numeric_keypad_overlay.h"

#include <cstring>

#include "../fonts/fonts.h"
#include "../theme.h"
#include "button.h"
#include "numeric_keypad.h"

namespace ui::components {

namespace {

constexpr int MAX_DIGITS = 15;  // generous hard ceiling regardless of maxLen — matches an lv_label's practical width here

struct OverlayState {
    lv_obj_t* overlay;
    lv_obj_t* textarea;
    lv_obj_t* doneBtn;
    lv_obj_t* hint;
    char digits[MAX_DIGITS + 1];
    char* initialText;  // heap copy — returned verbatim if Cancel is chosen
    int maxLen;
    int autoAcceptLen;
    NumericValidator validator;
    const char* invalidHint;
    NumericInputCallback cb;
    void* userCtx;
};

void finish(OverlayState* state, bool accepted) {
    const char* text = accepted ? state->digits : state->initialText;
    if (state->cb != nullptr) state->cb(state->userCtx, text, accepted);
    lv_obj_del(state->overlay);  // also frees state->initialText via the overlay's own LV_EVENT_DELETE handler below
}

// Re-checks validity against the current digit buffer and updates the
// Done button / hint label to match — called after every keypress and on
// an explicit Done tap, so both the auto-accept path and the manual
// fallback are gated by exactly the same rule.
bool isCurrentlyValid(OverlayState* state) {
    if (state->digits[0] == '\0') return false;  // nothing typed — never auto- or manually acceptable
    return state->validator == nullptr || state->validator(state->digits);
}

void refresh(OverlayState* state) {
    lv_textarea_set_text(state->textarea, state->digits);
    bool valid = isCurrentlyValid(state);
    setEnabled(state->doneBtn, valid);
    if (!valid && state->digits[0] != '\0' && state->invalidHint != nullptr) {
        lv_label_set_text(state->hint, state->invalidHint);
        lv_obj_clear_flag(state->hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(state->hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void onKeypad(void* ctx, char key) {
    auto* state = static_cast<OverlayState*>(ctx);
    size_t len = strlen(state->digits);
    if (key == 'C') {
        state->digits[0] = '\0';
    } else if (key == 'B') {
        if (len > 0) state->digits[len - 1] = '\0';
    } else if (static_cast<int>(len) < state->maxLen && static_cast<int>(len) < MAX_DIGITS) {
        state->digits[len] = key;
        state->digits[len + 1] = '\0';
    }
    refresh(state);

    len = strlen(state->digits);
    if (state->autoAcceptLen > 0 && static_cast<int>(len) == state->autoAcceptLen && isCurrentlyValid(state)) {
        finish(state, true);
    }
}

void overlayEventCb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    auto* state = static_cast<OverlayState*>(lv_event_get_user_data(e));
    delete[] state->initialText;
    delete state;
}

void doneClicked(lv_event_t* e) {
    auto* state = static_cast<OverlayState*>(lv_event_get_user_data(e));
    if (isCurrentlyValid(state)) finish(state, true);
}
void cancelClicked(lv_event_t* e) { finish(static_cast<OverlayState*>(lv_event_get_user_data(e)), false); }

}  // namespace

lv_obj_t* numericKeypadOverlay(const char* initialText, const char* placeholder, int maxLen, int autoAcceptLen,
                                NumericValidator validator, const char* invalidHint, NumericInputCallback cb,
                                void* ctx) {
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_add_style(overlay, theme::screenBg(), 0);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    auto* state = new OverlayState{};
    state->overlay = overlay;
    state->digits[0] = '\0';
    size_t initLen = strlen(initialText != nullptr ? initialText : "");
    state->initialText = new char[initLen + 1];
    strcpy(state->initialText, initialText != nullptr ? initialText : "");
    state->maxLen = (maxLen > 0 && maxLen <= MAX_DIGITS) ? maxLen : MAX_DIGITS;
    state->autoAcceptLen = autoAcceptLen;
    state->validator = validator;
    state->invalidHint = invalidHint;
    state->cb = cb;
    state->userCtx = ctx;
    lv_obj_add_event_cb(overlay, overlayEventCb, LV_EVENT_DELETE, state);

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
    lv_textarea_set_text(ta, "");
    if (placeholder != nullptr) lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_set_height(ta, 64);
    state->textarea = ta;

    lv_obj_t* cancelBtn = secondaryButton(topRow, "Cancel");
    lv_obj_set_height(cancelBtn, 64);
    lv_obj_add_event_cb(cancelBtn, cancelClicked, LV_EVENT_CLICKED, state);

    lv_obj_t* doneBtn = primaryButton(topRow, "Done");
    lv_obj_set_height(doneBtn, 64);
    lv_obj_add_event_cb(doneBtn, doneClicked, LV_EVENT_CLICKED, state);
    state->doneBtn = doneBtn;

    lv_obj_t* hint = lv_label_create(overlay);
    lv_obj_add_style(hint, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(hint, &ui_font_plex_400_14, 0);
    lv_obj_set_pos(hint, 8, 84);
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    state->hint = hint;

    lv_obj_t* keypadWrap = lv_obj_create(overlay);
    lv_obj_remove_style_all(keypadWrap);
    lv_obj_set_size(keypadWrap, LV_PCT(100), lv_disp_get_ver_res(nullptr) - 116);
    lv_obj_set_pos(keypadWrap, 0, 116);
    lv_obj_set_flex_flow(keypadWrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(keypadWrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(keypadWrap, LV_OBJ_FLAG_SCROLLABLE);
    numericKeypad(keypadWrap, onKeypad, state);

    refresh(state);
    return overlay;
}

}  // namespace ui::components
