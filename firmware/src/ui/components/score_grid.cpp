#include "score_grid.h"

#include <cstdio>
#include <cstdlib>

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

namespace {

constexpr int ROWS = 5;
constexpr int COLS = 5;
constexpr int CELLS = ROWS * COLS;
constexpr lv_coord_t CELL_SIZE = 62;  // outdoor one-handed minimum, per this task's 62-74px target range

struct GridState {
    ScoreCallback cb;
    void* userCtx;
    char labels[CELLS][4];         // "1".."25"
    const char* map[CELLS + ROWS];  // each row's cells + a "\n" entry between rows + terminator
};

void gridEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* matrix = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* state = static_cast<GridState*>(lv_event_get_user_data(e));

    if (code == LV_EVENT_DELETE) {
        delete state;
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED || state == nullptr || state->cb == nullptr) return;

    uint16_t id = lv_btnmatrix_get_selected_btn(matrix);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char* text = lv_btnmatrix_get_btn_text(matrix, id);
    if (text == nullptr) return;
    state->cb(state->userCtx, atoi(text));
}

}  // namespace

lv_obj_t* scoreGrid(lv_obj_t* parent, int initialValue, ScoreCallback cb, void* ctx) {
    auto* state = new GridState{};
    state->cb = cb;
    state->userCtx = ctx;

    int mapIndex = 0;
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int value = row * COLS + col + 1;
            snprintf(state->labels[value - 1], sizeof(state->labels[value - 1]), "%d", value);
            state->map[mapIndex++] = state->labels[value - 1];
        }
        state->map[mapIndex++] = "\n";
    }
    state->map[mapIndex - 1] = "";  // last "\n" becomes the terminator instead

    lv_obj_t* matrix = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(matrix, state->map);
    lv_btnmatrix_set_btn_ctrl_all(matrix, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(matrix, true);
    if (initialValue >= 1 && initialValue <= CELLS) {
        lv_btnmatrix_set_btn_ctrl(matrix, initialValue - 1, LV_BTNMATRIX_CTRL_CHECKED);
    }

    lv_obj_remove_style_all(matrix);
    lv_obj_add_style(matrix, theme::screenBg(), 0);
    lv_obj_set_style_pad_gap(matrix, 6, 0);
    lv_obj_set_size(matrix, COLS * (CELL_SIZE + 6), ROWS * (CELL_SIZE + 6));

    lv_obj_add_style(matrix, theme::raisedSurface(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(matrix, &ui_font_archivo_800_52, LV_PART_ITEMS);
    lv_obj_add_style(matrix, theme::btnPrimary(), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_add_event_cb(matrix, gridEventCb, LV_EVENT_ALL, state);
    return matrix;
}

}  // namespace ui::components
