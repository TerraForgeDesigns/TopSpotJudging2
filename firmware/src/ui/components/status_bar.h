// Persistent top status bar — 46px, present on every screen, built once by
// screen_manager::init() and never rebuilt (it isn't part of any screen's
// content tree, so screen push/pop never touches it). Device label and
// judge label on the left; connection state and battery on the right,
// each a colored dot plus a short uppercase label — matching DESIGN.md's
// component spec and LANGUAGE.md's exact recurring-state wording (Up to
// Date / Updating / Not Connected — the same words Home Base uses for the
// same states, see LANGUAGE.md's "Recurring state phrasing" section).
#pragma once

#include <lvgl.h>

namespace ui::components {

enum class ConnState { UpToDate, Updating, NotConnected };

void buildStatusBar(lv_obj_t* parent, lv_coord_t height);

// Device's own wire identity (e.g. "hh-1") — never blank; falls back to
// "Unnamed" rather than showing nothing if it hasn't been set yet.
void setDeviceLabel(const char* text);

// The judge currently signed in on this device, if any; nullptr or ""
// shows nothing (no judge assigned yet is a normal state, not an error).
void setJudgeLabel(const char* text);

void setConnState(ConnState state);

// -1 = unknown (shows "--" instead of a misleading "0%").
void setBatteryPct(int pct);

}  // namespace ui::components
