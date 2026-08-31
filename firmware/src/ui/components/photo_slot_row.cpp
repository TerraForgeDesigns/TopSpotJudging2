#include "photo_slot_row.h"

#include "../fonts/fonts.h"
#include "../theme.h"
#include "button.h"

namespace ui::components {

namespace {

struct SlotCtx {
    PhotoSlotCallback cb;
    void* userCtx;
};

void actionEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    auto* ctx = static_cast<SlotCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_DELETE) {
        delete ctx;
        return;
    }
    if (code == LV_EVENT_CLICKED && ctx->cb != nullptr) ctx->cb(ctx->userCtx);
}

}  // namespace

lv_obj_t* photoSlotRow(lv_obj_t* parent, const char* label, PhotoSlotState state, const char* actionLabel,
                        PhotoSlotCallback cb, void* ctx) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, theme::raisedSurface(), 0);
    lv_obj_set_size(row, LV_PCT(100), 74);
    lv_obj_set_style_pad_hor(row, 16, 0);
    // See list_row.cpp's comment — no margin property in LVGL v8; spacing
    // comes from the parent's pad_row (every current caller sets one).
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(left);
    lv_obj_add_style(titleLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(titleLabel, &ui_font_plex_400_16, 0);
    lv_label_set_text(titleLabel, label);

    lv_obj_t* statusRow = lv_obj_create(left);
    lv_obj_remove_style_all(statusRow);
    lv_obj_set_size(statusRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(statusRow, 6, 0);
    lv_obj_clear_flag(statusRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dot = lv_obj_create(statusRow);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(dot, state == PhotoSlotState::Taken ? theme::statusGood() : theme::statusBad(), 0);

    lv_obj_t* statusLabel = lv_label_create(statusRow);
    lv_obj_add_style(statusLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(statusLabel, &ui_font_plex_400_14, 0);
    lv_label_set_text(statusLabel, state == PhotoSlotState::Taken ? "Taken" : "Missing");

    lv_obj_t* btn = secondaryButton(row, actionLabel);
    lv_obj_set_height(btn, 56);
    auto* slotCtx = new SlotCtx{cb, ctx};
    lv_obj_add_event_cb(btn, actionEventCb, LV_EVENT_ALL, slotCtx);

    return row;
}

}  // namespace ui::components
