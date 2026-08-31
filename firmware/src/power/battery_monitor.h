// Battery-low warnings — a pure function of a percentage. Deliberately
// does NOT include power/battery.h and never will on its own: that file
// hard-fails to compile until a real PIN_BATTERY_ADC is confirmed (its
// own header comment: a wrong guess "risks actual hardware damage"), and
// this module must stay buildable regardless of whether that's happened
// yet. The one place a real reading is wired in (behind
// `#ifdef PIN_BATTERY_ADC`) is bringup_judging.cpp's loop — see
// DECISIONS.md's F7 entry. Until then, every call here is `check(-1)`
// ("unknown"), which never warns — no fabricated percentage stands in
// for real hardware that doesn't exist yet, same principle as this
// project's `battery_pct: null` sync payload (F5) and F6's
// mains-power-detect note.
#pragma once

namespace power::battery_monitor {

// `percent`: 0-100, or -1 for "unknown" (today's only real value — see
// header comment above). Shows a toast on crossing INTO a lower tier
// only — tracks the last-warned tier internally so it never repeats on
// every call. Call this as often as convenient (e.g. once per Home
// screen build); it's cheap and self-debouncing.
void check(int percent);

}  // namespace power::battery_monitor
