#include "checklist_row.h"

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

namespace {

struct CheckCtx {
    ChecklistCallback cb;
    void* userCtx;
};

void checkEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* cb = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* ctx = static_cast<CheckCtx*>(lv_event_get_user_data(e));

    if (code == LV_EVENT_DELETE) {
        delete ctx;
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED || ctx->cb == nullptr) return;
    ctx->cb(ctx->userCtx, lv_obj_has_state(cb, LV_STATE_CHECKED));
}

}  // namespace

lv_obj_t* checklistRow(lv_obj_t* parent, const char* label, bool initialChecked, ChecklistCallback cb, void* ctx) {
    lv_obj_t* box = lv_checkbox_create(parent);
    lv_checkbox_set_text(box, label);
    lv_obj_set_size(box, LV_PCT(100), 62);  // outdoor one-handed minimum tap height for the whole row
    lv_obj_set_style_text_font(box, &ui_font_plex_400_16, 0);
    lv_obj_add_style(box, theme::textPrimary(), 0);

    // Indicator box: unchecked = a raised-surface square; checked = gold,
    // per DESIGN.md's own token description ("gold-500 ... active states"
    // — a checked control's state, not one of the judged/pending/conflict
    // statuses the "never gold for status" rule is actually about).
    lv_obj_add_style(box, theme::raisedSurface(), LV_PART_INDICATOR);
    lv_obj_add_style(box, theme::btnPrimary(), LV_PART_INDICATOR | LV_STATE_CHECKED);

    if (initialChecked) lv_obj_add_state(box, LV_STATE_CHECKED);

    auto* checkCtx = new CheckCtx{cb, ctx};
    lv_obj_add_event_cb(box, checkEventCb, LV_EVENT_ALL, checkCtx);
    return box;
}

}  // namespace ui::components
