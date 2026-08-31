#include "score_row.h"

#include <cstdio>
#include <cstring>

#include "../fonts/fonts.h"
#include "../theme.h"
#include "button.h"

namespace ui::components {

namespace {

constexpr int MAX_RANGE = 10;

struct RowState {
    ScoreCallback cb;
    void* userCtx;
    char labels[MAX_RANGE][4];       // "1".."10"
    const char* map[MAX_RANGE + 1];  // + null terminator entry
    int rangeMax;
};

void rowEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* matrix = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* state = static_cast<RowState*>(lv_event_get_user_data(e));

    if (code == LV_EVENT_DELETE) {
        delete state;
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED || state == nullptr || state->cb == nullptr) return;

    uint16_t id = lv_btnmatrix_get_selected_btn(matrix);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    state->cb(state->userCtx, id + 1);
}

}  // namespace

lv_obj_t* scoreRow(lv_obj_t* parent, int rangeMax, int initialValue, ScoreCallback cb, void* ctx) {
    if (rangeMax > MAX_RANGE) rangeMax = MAX_RANGE;  // caller error — score_grid.h is for 25; clamp rather than crash

    auto* state = new RowState{};
    state->cb = cb;
    state->userCtx = ctx;
    state->rangeMax = rangeMax;
    for (int i = 0; i < rangeMax; i++) {
        snprintf(state->labels[i], sizeof(state->labels[i]), "%d", i + 1);
        state->map[i] = state->labels[i];
    }
    state->map[rangeMax] = "";  // lv_btnmatrix map terminator

    lv_obj_t* matrix = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(matrix, state->map);
    lv_btnmatrix_set_btn_ctrl_all(matrix, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(matrix, true);
    if (initialValue >= 1 && initialValue <= rangeMax) {
        lv_btnmatrix_set_btn_ctrl(matrix, initialValue - 1, LV_BTNMATRIX_CTRL_CHECKED);
    }

    lv_obj_remove_style_all(matrix);
    lv_obj_add_style(matrix, theme::screenBg(), 0);
    lv_obj_set_style_pad_gap(matrix, 8, 0);
    lv_obj_set_size(matrix, LV_PCT(100), BUTTON_H);

    lv_obj_add_style(matrix, theme::raisedSurface(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(matrix, &ui_font_archivo_700_30, LV_PART_ITEMS);
    lv_obj_add_style(matrix, theme::btnPrimary(), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_add_event_cb(matrix, rowEventCb, LV_EVENT_ALL, state);
    return matrix;
}

}  // namespace ui::components
