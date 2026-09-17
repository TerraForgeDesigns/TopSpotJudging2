#include "ui_theme.h"

lv_color_t ts_color(uint32_t hex)
{
    return lv_color_make((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff);
}

static void reset_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
}

void ts_style_screen(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, ts_color(TS_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
}

lv_obj_t *ts_page(lv_obj_t *screen)
{
    lv_obj_t *page = lv_obj_create(screen);
    reset_obj(page);
    lv_obj_set_size(page, 944, 540);
    lv_obj_align(page, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 16, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t *ts_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, ts_color(color), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 0, LV_PART_MAIN);
    return label;
}

lv_obj_t *ts_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    reset_obj(card);
    lv_obj_set_style_bg_color(card, ts_color(TS_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, ts_color(TS_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 18, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, ts_color(0x020617), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t *ts_button(lv_obj_t *parent, const char *text, int32_t width, bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 58);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, ts_color(primary ? TS_COLOR_ACCENT : TS_COLOR_SURFACE_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, ts_color(TS_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, primary ? 14 : 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(button, ts_color(TS_COLOR_ACCENT_DARK), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(button, primary ? LV_OPA_30 : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, ts_color(primary ? TS_COLOR_ACCENT_DARK : 0x223049), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_90, LV_STATE_PRESSED);

    lv_obj_t *label = ts_label(button, text, &lv_font_montserrat_20, primary ? 0x111827 : TS_COLOR_TEXT);
    lv_obj_center(label);
    return button;
}

void ts_set_button_checked(lv_obj_t *button, bool checked)
{
    if (checked) {
        lv_obj_add_state(button, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(button, LV_STATE_CHECKED);
    }
}
