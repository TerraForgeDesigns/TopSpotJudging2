#pragma once

#include <stdint.h>

#include "lvgl.h"

#define TS_COLOR_BG 0x0b1020
#define TS_COLOR_SURFACE 0x111827
#define TS_COLOR_SURFACE_2 0x172033
#define TS_COLOR_BORDER 0x2a3852
#define TS_COLOR_TEXT 0xf8fafc
#define TS_COLOR_MUTED 0xa8b3c7
#define TS_COLOR_DIM 0x64748b
#define TS_COLOR_ACCENT 0xf8c14a
#define TS_COLOR_ACCENT_DARK 0xd99b23
#define TS_COLOR_CYAN 0x38bdf8
#define TS_COLOR_GREEN 0x22c55e
#define TS_COLOR_RED 0xef4444

lv_color_t ts_color(uint32_t hex);
void ts_style_screen(lv_obj_t *screen);
lv_obj_t *ts_page(lv_obj_t *screen);
lv_obj_t *ts_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color);
lv_obj_t *ts_card(lv_obj_t *parent);
lv_obj_t *ts_button(lv_obj_t *parent, const char *text, int32_t width, bool primary);
void ts_set_button_checked(lv_obj_t *button, bool checked);
