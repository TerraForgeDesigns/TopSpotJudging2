#include "modal_confirm.h"

#include "../fonts/fonts.h"
#include "../theme.h"
#include "button.h"

namespace ui::components {

namespace {

struct ModalState {
    lv_obj_t* overlay;
    ModalConfirmCallback cb;
    void* userCtx;
};

void finish(ModalState* state, bool confirmed) {
    if (state->cb != nullptr) state->cb(state->userCtx, confirmed);
    lv_obj_del(state->overlay);
}

void overlayDeleteCb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    delete static_cast<ModalState*>(lv_event_get_user_data(e));
}

void cancelClicked(lv_event_t* e) { finish(static_cast<ModalState*>(lv_event_get_user_data(e)), false); }
void confirmClicked(lv_event_t* e) { finish(static_cast<ModalState*>(lv_event_get_user_data(e)), true); }

}  // namespace

lv_obj_t* modalConfirm(const char* title, const char* body, const char* confirmLabel, bool destructive,
                        ModalConfirmCallback cb, void* ctx) {
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);  // dim scrim — the only intentionally raw color in this
                                                       // library: it's black-with-alpha, not a DESIGN.md token
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    auto* state = new ModalState{overlay, cb, ctx};
    lv_obj_add_event_cb(overlay, overlayDeleteCb, LV_EVENT_DELETE, state);

    lv_obj_t* dialog = lv_obj_create(overlay);
    lv_obj_remove_style_all(dialog);
    lv_obj_add_style(dialog, theme::card(), 0);
    lv_obj_set_style_pad_all(dialog, 20, 0);
    lv_obj_set_style_pad_row(dialog, 14, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(dialog, 480, LV_SIZE_CONTENT);
    lv_obj_center(dialog);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(dialog);
    lv_obj_add_style(titleLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(titleLabel, &ui_font_archivo_700_23, 0);
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(titleLabel, LV_PCT(100));
    lv_label_set_text(titleLabel, title);

    lv_obj_t* bodyLabel = lv_label_create(dialog);
    lv_obj_add_style(bodyLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(bodyLabel, &ui_font_plex_400_16, 0);
    lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bodyLabel, LV_PCT(100));
    lv_label_set_text(bodyLabel, body);

    lv_obj_t* btnRow = lv_obj_create(dialog);
    lv_obj_remove_style_all(btnRow);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, 10, 0);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancelBtn = secondaryButton(btnRow, "Cancel");
    lv_obj_add_event_cb(cancelBtn, cancelClicked, LV_EVENT_CLICKED, state);

    lv_obj_t* confirmBtn = destructive ? lv_btn_create(btnRow) : primaryButton(btnRow, confirmLabel);
    if (destructive) {
        lv_obj_remove_style_all(confirmBtn);
        lv_obj_add_style(confirmBtn, theme::btnDestructive(), 0);
        lv_obj_set_height(confirmBtn, BUTTON_H);
        lv_obj_set_style_pad_hor(confirmBtn, 28, 0);
        lv_obj_t* label = lv_label_create(confirmBtn);
        lv_obj_set_style_text_font(label, &ui_font_plex_600_23, 0);
        lv_label_set_text(label, confirmLabel);
        lv_obj_center(label);
    }
    lv_obj_add_event_cb(confirmBtn, confirmClicked, LV_EVENT_CLICKED, state);

    return overlay;
}

}  // namespace ui::components
