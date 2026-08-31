// A small captured log the diagnostics screen can show on-device — see
// ui/screens/diagnostics_screen.h. Deliberately NOT a retrofit of every
// Serial.printf() call in this codebase (that would be a large, mostly
// out-of-scope refactor for what this task actually needs) — wired only
// into the highest-value points: camera capture failures (this is also
// what backs camera::lastError() — one mechanism serves both),
// SD mount/unmount, and network::sync's / photo_upload's attempt
// outcomes. Everywhere else keeps calling Serial.printf() directly,
// unchanged, same as before this file existed.
#pragma once

namespace diag {

// Serial.printf()s `fmt`/varargs (unchanged behavior for anyone watching
// the serial monitor) AND appends the formatted line to a small ring
// buffer the diagnostics screen reads via snapshot() below. Safe to call
// from anywhere already calling Serial.printf() today — same
// single-threaded-UI-thread assumption every other non-storage,
// non-network module in this firmware already makes; network::sync's and
// photo_upload's background tasks must NOT call this directly (matches
// their own "no storage/LVGL from the task" rule) — log from their
// main-thread apply step instead, same as every other side effect they
// have.
void log(const char* fmt, ...);

// Copies the ring buffer's current contents (oldest first, newline-
// joined) into `out` (up to `outSize - 1` bytes, null-terminated) — for
// the diagnostics screen's log viewer. Never blocks, never allocates.
void snapshot(char* out, unsigned int outSize);

}  // namespace diag
