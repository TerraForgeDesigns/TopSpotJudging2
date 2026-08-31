#include "button.h"

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

namespace {

lv_obj_t* buildButton(lv_obj_t* parent, const char* label, lv_coord_t width, lv_style_t* normal,
                       lv_style_t* pressed) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, normal, 0);
    lv_obj_add_style(btn, pressed, LV_STATE_PRESSED);
    lv_obj_set_height(btn, BUTTON_H);
    if (width == LV_SIZE_CONTENT) {
        lv_obj_set_width(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(btn, 28, 0);
    } else {
        lv_obj_set_width(btn, width);
    }

    lv_obj_t* text = lv_label_create(btn);
    lv_obj_set_style_text_font(text, &ui_font_plex_600_23, 0);
    lv_label_set_text(text, label);
    lv_obj_center(text);
    return btn;
}

}  // namespace

lv_obj_t* primaryButton(lv_obj_t* parent, const char* label, lv_coord_t width) {
    return buildButton(parent, label, width, theme::btnPrimary(), theme::btnPrimaryPressed());
}

lv_obj_t* secondaryButton(lv_obj_t* parent, const char* label, lv_coord_t width) {
    return buildButton(parent, label, width, theme::btnSecondary(), theme::btnSecondaryPressed());
}

void setEnabled(lv_obj_t* button, bool enabled) {
    if (enabled) {
        lv_obj_clear_state(button, LV_STATE_DISABLED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
    }
}

}  // namespace ui::components
