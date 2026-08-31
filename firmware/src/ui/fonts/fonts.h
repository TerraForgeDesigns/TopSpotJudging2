// Embedded LVGL fonts — converted from the exact same font files Home Base
// self-hosts (homebase/app/static/fonts/*.woff2 -> decompressed to .ttf via
// fonttools -> converted via lv_font_conv), so the handheld renders the
// identical Archivo/IBM Plex Sans typefaces DESIGN.md specifies for both
// platforms, not a substitute. See DECISIONS.md for the exact conversion
// commands and glyph-range rationale.
//
// Only these 10 (family, weight, size) combinations exist — deliberately
// not every weight at every size. Each is embedded only because a specific
// component in src/ui/components/ actually renders text at that exact
// combination; DESIGN.md warns fonts are a large flash consumer on a 4MB
// partition, so nothing here is speculative. Glyph range is ASCII
// 0x20-0x7E only (95 printable characters) — LANGUAGE.md's handheld
// vocabulary is plain English with no em-dashes, curly quotes, or accented
// characters, so extending the range would cost flash for glyphs nothing
// ever draws.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// IBM Plex Sans — body text, field/list labels, secondary emphasis.
extern const lv_font_t ui_font_plex_400_14;  // micro / hint / caption text
extern const lv_font_t ui_font_plex_400_16;  // body text (DESIGN.md's 16px body minimum)
extern const lv_font_t ui_font_plex_500_19;  // status bar labels, field labels (uppercase, tracked)
extern const lv_font_t ui_font_plex_600_23;  // button labels, emphasized list-row title

// Archivo — headings, section titles, and every big number (scores, car
// numbers, keypad digits) — DESIGN.md: "the largest type on any handheld
// screen must be the data, never a label."
extern const lv_font_t ui_font_archivo_600_19;  // score-row / data-row category label
extern const lv_font_t ui_font_archivo_700_23;  // screen headings
extern const lv_font_t ui_font_archivo_700_30;  // section titles, score-row values
extern const lv_font_t ui_font_archivo_800_46;  // numeric readout (status bar / header numbers)
extern const lv_font_t ui_font_archivo_800_52;  // score grid & keypad digits
extern const lv_font_t ui_font_archivo_800_70;  // hero score number (biggest readout on any screen)

#ifdef __cplusplus
}
#endif
