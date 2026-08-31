// WiFi sync — the scan-first, backoff-aware routine PROTOCOL.md's
// "Handheld sync trigger model" section and this module's own
// DECISIONS.md entry (F5) describe. Three triggers, one routine:
//   (a) a car was just finished (ui::judging::finish() calls
//       requestNow() directly) — runs immediately, ignoring backoff.
//   (b) a periodic LVGL timer, base interval from
//       storage::Settings::syncIntervalSeconds, doubling (capped at
//       15 min) on a miss, reset to base on a hit. The ONLY trigger that
//       ever mutates storage::SyncState::currentRetryIntervalSeconds.
//   (c) the judge taps Update Now (home_screen.cpp calls requestNow()).
//
// CONTEXT.md: judges roam an open field and are REGULARLY out of WiFi
// range — that's the normal operating condition this module is built
// around, not an error state. A missed sync NEVER drops a queued car;
// only an explicit acknowledgement from Home Base does (see
// PROTOCOL.md's idempotency section and this module's .cpp).
//
// Architecture: the actual scan/connect/HTTP round trip runs on an
// isolated FreeRTOS task (same "don't block the single-threaded UI"
// principle as camera.cpp) — but unlike camera.cpp, the main/LVGL thread
// does NOT block waiting for it, because a periodic sync can fire
// unprompted while the judge is actively doing something else entirely;
// blocking touch input for the 1-5 seconds a real attempt can take would
// be a worse regression than camera.cpp's judge-initiated foreground
// wait. A short-period LVGL timer polls for the task's completion and
// applies the result (storage writes, status bar, toasts, the
// full-screen Scoring Updated notice) on the main thread only — LVGL
// isn't thread-safe, and neither is concurrent SD-card access, so the
// background task touches NEITHER: it receives an already-built request
// body and returns only raw response bytes, nothing more.
#pragma once

namespace network::sync {

// Once at boot, after ui::screen_manager::init() (this creates LVGL
// timers, and a result-apply may call the status bar's setters — both
// need the status bar to already exist) and after storage::vehicle_db::
// init() (a successful sync may call storage::vehicle_db::
// saveLearnedVehicle()). Sets up the periodic timer and the
// completion-poll timer; does not touch WiFi yet.
void init();

// Triggers (a) and (c): runs the full routine immediately, ignoring
// whatever the periodic backoff interval currently is, and — critically —
// never adjusts that interval either way, regardless of hit or miss. If
// an attempt is already in flight, this is a no-op rather than queuing a
// second one; nothing is lost by that, since the routine runs again on
// its own soon enough (the periodic timer, or the next requestNow()).
void requestNow();

}  // namespace network::sync
