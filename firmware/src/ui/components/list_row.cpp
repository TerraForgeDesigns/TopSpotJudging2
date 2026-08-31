#include "list_row.h"

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

namespace {

constexpr lv_coord_t ROW_H = 62;

struct RowCtx {
    ListRowCallback cb;
    void* userCtx;
};

void rowEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    auto* ctx = static_cast<RowCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_DELETE) {
        delete ctx;
        return;
    }
    if (code == LV_EVENT_CLICKED && ctx->cb != nullptr) ctx->cb(ctx->userCtx);
}

}  // namespace

lv_obj_t* listRow(lv_obj_t* parent, const char* title, const char* value, RowDot dot, ListRowCallback cb,
                   void* ctx) {
    lv_obj_t* row = lv_btn_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, theme::raisedSurface(), 0);
    lv_obj_add_style(row, theme::btnSecondaryPressed(), LV_STATE_PRESSED);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_pad_hor(row, 16, 0);
    // No per-row spacing here — LVGL v8 has no margin property (only
    // padding, which insets a widget's own content, not the gap between
    // siblings). Spacing between rows comes from the PARENT's pad_row —
    // every caller of listRow() so far (card(), and screen content
    // containers) already sets one.
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    if (dot != RowDot::None) {
        lv_obj_t* dotObj = lv_obj_create(left);
        lv_obj_remove_style_all(dotObj);
        lv_obj_set_size(dotObj, 10, 10);
        lv_obj_set_style_radius(dotObj, LV_RADIUS_CIRCLE, 0);
        lv_style_t* dotStyle = dot == RowDot::Good ? theme::statusGood()
                               : dot == RowDot::Pending ? theme::statusPending()
                                                         : theme::statusBad();
        lv_obj_add_style(dotObj, dotStyle, 0);
    }

    lv_obj_t* titleLabel = lv_label_create(left);
    lv_obj_add_style(titleLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(titleLabel, &ui_font_plex_400_16, 0);
    lv_label_set_text(titleLabel, title);

    if (value != nullptr) {
        lv_obj_t* valueLabel = lv_label_create(row);
        lv_obj_add_style(valueLabel, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(valueLabel, &ui_font_plex_400_16, 0);
        lv_label_set_text(valueLabel, value);
    }

    auto* rowCtx = new RowCtx{cb, ctx};
    lv_obj_add_event_cb(row, rowEventCb, LV_EVENT_ALL, rowCtx);
    return row;
}

}  // namespace ui::components
