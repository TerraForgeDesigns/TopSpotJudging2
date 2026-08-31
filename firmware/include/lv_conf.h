// LVGL v8.3 configuration for the handheld — CrowPanel DIS08070H, 800x480
// RGB565 parallel panel driven by LovyanGFX (see src/ui/lvgl_port.cpp),
// ESP32-S3-WROOM-1-N4R8 (4MB flash / 8MB PSRAM).
//
// Deliberately trimmed from LVGL's stock lv_conf_template.h: every widget
// this firmware doesn't build a component around is compiled OUT (see
// "Widgets" below) — DESIGN.md and this task both call out flash as scarce
// on a 4MB partition, so nothing speculative gets built in. If a later
// screen genuinely needs, say, a chart, turn it on there — don't
// pre-enable it "just in case."
#pragma once

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
// RGB565 — the panel's native format (see lcd_config.h's Panel_RGB config).
// Matching LVGL's color depth to the panel's exactly means the flush
// callback is a straight memcpy into LovyanGFX, no per-pixel conversion —
// see lvgl_port.cpp and requirement 7's touch-to-visual latency budget.
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/
// LVGL's own object/style/animation heap lives in PSRAM, not the ESP32-S3's
// ~512KB internal SRAM — internal SRAM is needed for FreeRTOS task stacks,
// the camera/SD DMA buffers, and the network stack, all of which this
// project needs running at the same time as the UI. Hooked to
// heap_caps_malloc(..., MALLOC_CAP_SPIRAM) in lvgl_port.cpp.
// LV_MEM_CUSTOM_INCLUDE is pulled into lv_mem.c, which is compiled as
// plain C — these three names must be plain C-linkage functions (declared
// `extern "C"` in lvgl_psram_alloc.h), never a C++ namespaced symbol, or
// the macro expansion below fails to compile.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "ui/lvgl_psram_alloc.h"
#define LV_MEM_CUSTOM_ALLOC lvgl_psram_malloc
#define LV_MEM_CUSTOM_FREE lvgl_psram_free
#define LV_MEM_CUSTOM_REALLOC lvgl_psram_realloc

#define LV_MEM_ADR 0
#define LV_MEM_AUTO_DEFRAG 1

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 16  /*ms — ~60Hz, matches the panel's own scanout rate*/
#define LV_INDEV_DEF_READ_PERIOD 16 /*ms — touch poll cadence, see requirement 7*/
// NOT LV_TICK_CUSTOM: Arduino.h is a C++ header (String, Print, ...) and
// lv_tick.c is compiled as plain C, so pulling Arduino.h into it via
// LV_TICK_CUSTOM_INCLUDE doesn't compile. lvgl_port.cpp calls lv_tick_inc()
// itself from a FreeRTOS timer instead — see its header comment.
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130 /*7" @ 800x480 ~ 128 DPI; used only for LVGL's internal size defaults*/

/*=======================
   FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_PERF_MONITOR 0 /*flip on temporarily on real hardware for requirement 7's FPS check*/
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4

#define LV_USE_GPU 0 /*no PPA/DMA2D use yet — revisit if requirement 7's budget is missed*/

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

/*=====================
   COMPILER SETTINGS
 *=====================*/
#define LV_BIG_ENDIAN_SYSTEM 0
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_USE_LARGE_COORD 0

/*==================
   FONT USAGE
 *==================*/
// Every LVGL built-in (Montserrat) font is off — this firmware never draws
// with one. All real text uses the embedded Archivo/IBM Plex Sans set in
// src/ui/fonts/ (see fonts.h), matching DESIGN.md exactly instead of a
// generic UI typeface. LV_FONT_DEFAULT must still resolve to something at
// compile time (LVGL's default theme references it internally) — pointed
// at the smallest real body font rather than compiling in a Montserrat
// nobody uses.
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0

// lv_conf.h is parsed by lv_conf_internal.h before lvgl's own font headers
// declare lv_font_t — plain `extern lv_font_t ...` here fails with
// "'lv_font_t' does not name a type". Forward-declaring the same
// incomplete struct LVGL's own lv_font.h eventually typedefs is the
// documented fix (this exact snippet is LVGL's own recommended pattern
// for pointing LV_FONT_DEFAULT at a custom font).
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _lv_font_t lv_font_t;
extern const lv_font_t ui_font_plex_400_16;  // must match fonts/ui_font_plex_400_16.c's `const` qualifier exactly
#ifdef __cplusplus
}
#endif
#define LV_FONT_DEFAULT (&ui_font_plex_400_16)

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
   TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
   WIDGET USAGE
 *==================*/
// Only what src/ui/components/ actually builds on. Everything else
// (charts, calendars, spinboxes, tabview, tileview, colorwheel, LED,
// meter, QR code, GIF, menu, span, window, roller, dropdown, slider,
// switch...) is off — see this file's header comment.
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_BAR 1        /*battery/progress fills*/
#define LV_USE_BTN 1        /*primary/secondary buttons*/
#define LV_USE_BTNMATRIX 1  /*backs the numeric keypad and score grid*/
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 1   /*checklist row*/
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1        /*car/judge-sheet photos, status-bar icons*/
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 1
#define LV_USE_LINE 1
#define LV_USE_LIST 0        /*custom list row component instead — full DESIGN.md token control*/
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0      /*custom modal confirm instead*/
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0     /*would need LV_USE_ARC too — not worth two more widgets for a component that never ended up needing one*/
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 1    /*text keyboard wrapper, search field*/
#define LV_TEXTAREA_DEF_PWD_SHOW_TIME 0
#define LV_USE_TABLE 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_LED 0
#define LV_USE_CHART 0
#define LV_USE_CALENDAR 0
#define LV_USE_KEYBOARD 1    /*text keyboard wrapper*/
#define LV_USE_IMGBTN 0
#define LV_USE_QRCODE 0
#define LV_USE_SPAN 0
#define LV_USE_ANIMIMG 0

/*==================
 * THEMES
 *==================*/
// LVGL's default theme is off — src/ui/theme.cpp applies DESIGN.md's own
// token styles directly (see requirement 2: "a screen must never reference
// a raw colour"), not LVGL's stock look reskinned. Basic theme (required
// as SOME theme must exist) stays on but is never actually applied.
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 0

/*==================
 * LAYOUTS
 *==================*/
#define LV_USE_FLEX 1  /*status bar, score grid rows, list layouts*/
#define LV_USE_GRID 1  /*numeric keypad, score grid*/

/*==================
 * 3RD PARTY LIBRARIES
 *==================*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
// PHOTOS screen preview (F3) needs this — displays the JPEG just written
// by camera::captureToFile() straight from a PSRAM buffer (no LVGL
// filesystem driver needed — see ui/screens/photos_screen.cpp). Flash
// cost is the one real feature-flag change since F2; see DECISIONS.md.
#define LV_USE_SJPG 1
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

/*==================
 * OTHERS
 *==================*/
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_MSG 0
#define LV_USE_IME_PINYIN 0

/*==================
 * EXAMPLES
 *==================*/
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0
