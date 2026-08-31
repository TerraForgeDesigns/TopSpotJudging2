// The runtime, switchable theme for the LVGL component library — DESIGN.md
// token colors as lv_color_t, in two full variants (dark default + a
// high-contrast Daylight Mode for direct sun — see DESIGN.md's handheld
// section), switchable at runtime with no per-screen code changes and no
// screen ever calling lv_color_hex()/lv_palette_main() directly.
//
// This is a SEPARATE module from display/theme.h, which stays exactly as
// it was: a handful of raw RGB565 uint16_t constants used only by the
// isolated (non-LVGL) bringup_display.cpp test. Nothing here reuses those
// values or that header — this one is the real, LVGL-facing, dual-variant,
// runtime-switchable system every UI component built from here on must
// use instead.
#pragma once

#include <lvgl.h>

namespace ui::theme {

enum class Mode { Dark, Daylight };

// One full DESIGN.md palette. Field names match DESIGN.md's token table
// exactly so a reviewer can check this against DESIGN.md line by line.
struct Palette {
    lv_color_t ink900;    // app background, deepest
    lv_color_t ink800;    // card / panel surface
    lv_color_t ink700;    // raised surface, inputs
    lv_color_t ink600;    // borders, dividers
    lv_color_t ink400;    // disabled text, subtle icons only

    lv_color_t textPrimary;
    lv_color_t textSecondary;
    lv_color_t textOnGold;  // always dark #0E1116 — never white on a gold fill, in either theme

    lv_color_t gold500;  // primary brand accent — buttons, active states, focus. NEVER a status.
    lv_color_t gold400;  // hover/active state of gold elements

    lv_color_t green500;  // status: judged / up to date / success
    lv_color_t blue500;   // status: updating / informational
    lv_color_t red500;    // status: conflict / error / unjudged-critical
};

// One-time setup: builds both palettes, initializes the shared style
// objects every component pulls from, and loads the persisted mode choice
// from SD (defaults to Dark if no preference file exists yet, or the card
// isn't readable — see CONTEXT.md's offline-first/resilience principle:
// a missing preference is never a startup failure). Call once, after
// storage::begin() and ui::lvglInit().
void init();

Mode currentMode();
const Palette& colors();

// Switches the live palette, restyles every currently-visible object that
// uses one of the shared styles below (via lv_obj_report_style_change —
// no screen needs to rebuild or re-fetch anything), and persists the
// choice to SD ("/prefs/theme.txt") so it survives a restart.
void setMode(Mode mode);

// ---------------------------------------------------------------------
// Shared, theme-aware style objects. Every component in ui/components/
// applies one of these via lv_obj_add_style() instead of setting colors
// on itself directly — that's what makes setMode() able to retheme a
// live screen with zero per-screen code.
// ---------------------------------------------------------------------
lv_style_t* screenBg();        // full-screen container background (ink900)
lv_style_t* card();            // ink800 surface, ink600 1px border, rounded corners
lv_style_t* raisedSurface();   // ink700 — inputs, raised panels
lv_style_t* btnPrimary();      // gold fill, dark text, per DESIGN.md's contrast rule
lv_style_t* btnPrimaryPressed();
lv_style_t* btnSecondary();    // transparent fill, ink600 border, text primary
lv_style_t* btnSecondaryPressed();
lv_style_t* btnDestructive();  // filled red — confirm-dialog-only, per DESIGN.md's destructive-button rule
lv_style_t* textPrimary();     // label style: text-primary color, body font
lv_style_t* textSecondary();   // label style: text-secondary color, body font
lv_style_t* divider();         // 1px ink600 line

// Status dots/fills — DESIGN.md's hard rule: status is ALWAYS one of these
// three, NEVER gold. Named to match Home Base's own pill vocabulary
// (pill--judged / pill--pending / pill--conflict) so the same semantic
// word means the same color on both platforms — see LANGUAGE.md's
// recurring-state-phrasing section.
lv_style_t* statusGood();     // green — judged / up to date
lv_style_t* statusPending();  // blue — updating / informational
lv_style_t* statusBad();      // red — conflict / not connected / error

// Alert banners — same ink800 card surface, with a colored left border
// accent (matches Home Base's own .card--danger/.card--info pattern).
lv_style_t* bannerInfo();
lv_style_t* bannerCritical();

}  // namespace ui::theme
