// LVGL <-> LovyanGFX bridge. This is deliberately NOT a display driver —
// display/lcd_config.h + LovyanGFX already own every RGB-panel timing value
// and the GT911 touch driver, sourced from Elecrow's verified example (see
// pins.h). This file is the thin (~two callback) adapter DESIGN.md's
// "don't hand-roll a display driver" instruction is fine with: it hands
// LVGL's rendered pixels to LovyanGFX's already-verified pushImage(), and
// hands LovyanGFX's already-verified getTouch() to LVGL's input device —
// nothing here talks to a GPIO or a register.
#pragma once

namespace ui {

// Brings up display::begin() (if not already), initializes LVGL, allocates
// the PSRAM draw buffers, and registers the display + touch input driver.
// Call once at startup, after display::begin() would normally be called
// (this calls it internally if needed). Returns false if the panel failed
// to initialize.
bool lvglInit();

// Pumps LVGL's timer/animation/redraw handler and its own tick source.
// Call every loop() iteration — do not block loop() for more than a few ms
// at a time anywhere else in the firmware, or LVGL's animations and touch
// responsiveness (requirement 7's ~100ms budget) suffer.
void lvglPump();

}  // namespace ui
