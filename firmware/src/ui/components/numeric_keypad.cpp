#include "numeric_keypad.h"

#include <cstring>

#include "../fonts/fonts.h"
#include "../theme.h"
#include "button.h"

namespace ui::components {

namespace {

// lv_btnmatrix keeps a POINTER to this map, not a copy — must outlive
// every keypad instance, hence static (read-only literals, safe to share
// across instances since none of them mutate it). Plain "Delete" text,
// not LV_SYMBOL_BACKSPACE — the embedded fonts only cover ASCII 0x20-0x7E
// (see fonts/fonts.h), not LVGL's separate symbol-glyph codepoint range.
static const char* KEYPAD_MAP[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "Clear", "0", "Delete", "",
};

struct CallbackCtx {
    KeypadCallback cb;
    void* userCtx;
};

void matrixEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* matrix = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* ctx = static_cast<CallbackCtx*>(lv_event_get_user_data(e));

    if (code == LV_EVENT_DELETE) {
        delete ctx;
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    uint16_t id = lv_btnmatrix_get_selected_btn(matrix);
    const char* text = lv_btnmatrix_get_btn_text(matrix, id);
    if (text == nullptr || ctx == nullptr || ctx->cb == nullptr) return;

    char key;
    if (strcmp(text, "Clear") == 0) {
        key = 'C';
    } else if (strcmp(text, "Delete") == 0) {
        key = 'B';
    } else {
        key = text[0];  // a single ASCII digit
    }
    ctx->cb(ctx->userCtx, key);
}

}  // namespace

lv_obj_t* numericKeypad(lv_obj_t* parent, KeypadCallback cb, void* ctx) {
    lv_obj_t* matrix = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(matrix, KEYPAD_MAP);

    lv_obj_remove_style_all(matrix);
    lv_obj_add_style(matrix, theme::screenBg(), 0);
    lv_obj_set_style_pad_all(matrix, 6, 0);
    lv_obj_set_style_pad_gap(matrix, 8, 0);
    lv_obj_set_size(matrix, 3 * 90, 4 * BUTTON_H);

    lv_obj_add_style(matrix, theme::raisedSurface(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(matrix, &ui_font_archivo_700_23, LV_PART_ITEMS);
    lv_obj_add_style(matrix, theme::btnPrimaryPressed(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_style(matrix, theme::btnPrimaryPressed(), LV_PART_ITEMS | LV_STATE_CHECKED);

    auto* wrapped = new CallbackCtx{cb, ctx};
    lv_obj_add_event_cb(matrix, matrixEventCb, LV_EVENT_ALL, wrapped);
    return matrix;
}

}  // namespace ui::components
