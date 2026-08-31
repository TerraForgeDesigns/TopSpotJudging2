// Backlight dim/off on idle — the primary power lever available today.
// The RGB parallel panel is DMA-refreshed continuously and can't idle the
// way an SPI display can (see DECISIONS.md's F7 entry) — dimming/turning
// off GPIO2's backlight PWM is the one thing this task can safely do
// without a confirmed touch-wake GPIO (also DECISIONS.md's F7 entry: true
// deep sleep is deferred, not built here). CPU, touch polling, and the
// RGB DMA refresh all keep running through every state below — only the
// backlight LED current changes — so waking is always instant: the very
// next touch is already being polled normally, no special wake path
// needed.
#pragma once

namespace power::backlight {

// Once at boot, after ui::lvglInit() (needs the LVGL display/indev
// already registered — see lv_disp_get_inactive_time()'s own
// requirement) and ui::theme::init(). Starts a ~1s LVGL timer that reads
// LVGL's own input-inactivity clock against storage::Settings::
// backlightDimSeconds/backlightOffSeconds and calls
// display::setBacklight() on state transitions only (never every tick).
void init();

}  // namespace power::backlight
