#include "toast.h"

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

namespace {

lv_obj_t* g_activeToast = nullptr;    // at most one at a time — a second call replaces the first
lv_timer_t* g_activeTimer = nullptr;  // its pending auto-dismiss timer, tracked so an early replace can cancel it

void dismiss(lv_timer_t* timer) {
    lv_obj_t* toast = static_cast<lv_obj_t*>(timer->user_data);
    if (toast == g_activeToast) {
        g_activeToast = nullptr;
        g_activeTimer = nullptr;
    }
    lv_obj_del(toast);
    // The timer itself is a one-shot (repeat count 1, see showToast()
    // below) — LVGL deletes it automatically once its repeat count
    // reaches 0, so it must not be deleted again here.
}

}  // namespace

void showToast(const char* text, ToastSeverity severity, uint32_t durationMs) {
    if (g_activeToast != nullptr) {
        // Cancel the old toast's pending timer FIRST — otherwise it fires
        // later against an object this call is about to delete out from
        // under it (dismiss() would then be handed a dangling pointer).
        if (g_activeTimer != nullptr) lv_timer_del(g_activeTimer);
        lv_obj_del(g_activeToast);
        g_activeToast = nullptr;
        g_activeTimer = nullptr;
    }

    lv_obj_t* toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toast);
    lv_obj_add_style(toast, theme::card(), 0);
    lv_obj_set_style_pad_all(toast, 14, 0);
    lv_obj_set_flex_flow(toast, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toast, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(toast, 10, 0);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dot = lv_obj_create(toast);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(dot, severity == ToastSeverity::Success ? theme::statusGood() : theme::statusBad(), 0);

    lv_obj_t* label = lv_label_create(toast);
    lv_obj_add_style(label, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(label, &ui_font_plex_400_16, 0);
    lv_label_set_text(label, text);

    g_activeToast = toast;
    g_activeTimer = lv_timer_create(dismiss, durationMs, toast);
    lv_timer_set_repeat_count(g_activeTimer, 1);
}

}  // namespace ui::components
