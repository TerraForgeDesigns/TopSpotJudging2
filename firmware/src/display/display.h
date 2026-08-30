// Display subsystem — thin wrapper around the LGFX/GT911 device so the
// rest of the firmware (UI screens, bring-up tests) never touches
// LovyanGFX panel/timing config directly. Keeps lcd_config.h's
// verified-but-fiddly panel setup isolated to one place; only code that
// actually needs to draw (src/ui/) pulls in LovyanGFX transitively via
// this header. Camera/storage/power subsystems never need to.
#pragma once

#include <cstdint>

#include "lcd_config.h"  // defines the global-scope LGFX class

namespace display {

// Brings up the RGB LCD bus, panel, backlight, and GT911 touch. Safe to
// call once at startup. Returns false if the panel failed to initialize
// (LovyanGFX's init() reports failure) — the caller decides what to do
// (this subsystem never halts or reboots on its own).
bool begin();

// True once begin() has succeeded.
bool isReady();

// Raw drawing access for UI code — LovyanGFX's own API (fillRect,
// drawString, tabular text, image blits, ...) is already a good fit for
// this project, so we expose the device directly rather than re-wrapping
// every primitive.
LGFX& gfx();

// Non-blocking touch poll. Returns true and fills x/y (screen pixel
// coordinates, 0..799 / 0..479) if a touch is currently active.
bool getTouch(int32_t* x, int32_t* y);

void setBacklight(uint8_t brightness);  // 0-255

}  // namespace display
