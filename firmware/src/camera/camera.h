// Arducam Mega 5MP — capture subsystem.
//
// IMPORTANT: the Arducam_Mega library (v3.0.0) has no timeout on its
// internal register-poll wait (`waitI2cIdle` is a bare `while(...) ;`
// loop). If the camera is missing, unresponsive, or fails mid-operation,
// a direct call into the library can hang forever — worse than a crash,
// since it would freeze the entire single-threaded firmware (display,
// touch, SD, UI, everything) during setup().
//
// So every actual library call happens on an isolated FreeRTOS task. The
// caller polls with its own timeout; if the task doesn't finish in time,
// we forcibly vTaskDelete() it and report the camera unavailable rather
// than let the hang propagate. This is a deliberate response to a real
// risk found by reading the vendor library's source — see DECISIONS.md.
//
// Consequence the rest of the firmware must respect: camera support is
// OPTIONAL and its absence must never block booting or using the display,
// touch, SD, or judging UI — only the two required photo captures
// (vehicle + judge sheet) are gated on it. See CONTEXT.md workflow.
#pragma once

#include <cstdint>
#include <cstddef>

namespace camera {

// 1280x720 — a real-photo-quality mode for the judging flow's two
// required captures (see ui/screens/photos_screen.*), distinct from the
// bring-up test's CAM_IMAGE_MODE_QVGA (320x240, chosen there only for a
// fast round trip to verify wiring). NOT verified against real hardware
// at this resolution specifically — the bring-up test never exercised
// anything above QVGA — so treat this as a starting point to confirm
// (capture time, file size, SD write duration) once a board is
// available, not a settled/tuned value. See DECISIONS.md.
constexpr int CAPTURE_MODE_PHOTO = 0x08;  // CAM_IMAGE_MODE_HD, per Arducam_Mega's ArducamCamera.h

enum class CameraState {
    UNINITIALIZED,
    INITIALIZING,
    READY,
    UNAVAILABLE,  // init failed or timed out — no camera present/responding
};

// Starts camera initialization on an isolated task and returns
// immediately (non-blocking). Call waitReady() to block (with your own
// timeout) until it settles into READY or UNAVAILABLE.
void begin();

// Blocks up to timeoutMs polling for initialization to finish. Returns
// true if the camera is READY. Safe to call from setup() — will never
// hang past timeoutMs even if the camera never responds.
bool waitReady(uint32_t timeoutMs);

CameraState getState();
bool isReady();

// The GPIO this build is actually using for Camera CS (PIN_CAMERA_CS from
// pins.h) — logged to the serial console at begin() and shown on the
// diagnostics screen, so a mis-set CS pin (e.g. the PRE-SOLDER GATE
// fallback in pins.h chosen or not chosen correctly) is visible as "wrong
// pin" rather than presenting as a dead/unresponsive camera. See
// pins.h's PRE-SOLDER GATE block.
int csPin();

// The message from the most recent failure this module has seen — init
// failure/timeout (begin()/waitReady()) or a capture failure
// (captureToFile()) — persisted (unlike CaptureResult::error, which is
// only ever returned once from the call that produced it) so the
// diagnostics screen can show "camera status and last error" (see
// ui/screens/diagnostics_screen.h) after the fact, not just at the
// moment of failure. "" if nothing has failed yet since boot.
const char* lastError();

struct CaptureResult {
    bool success;
    uint32_t bytesWritten;
    const char* error;  // static string, safe to log/display directly

    CaptureResult(bool s = false, uint32_t bytes = 0, const char* err = "")
        : success(s), bytesWritten(bytes), error(err) {}
};

// Full capture-and-store sequence (see PROTOCOL.md-adjacent workflow in
// the project's hardware contract): capture a JPEG, pull it off the
// camera's FIFO, write it to `path` on the SD card, and verify the file
// landed. Requires storage::begin() to have already succeeded. Runs on an
// isolated task with its own timeout, same rationale as begin() above —
// a capture that stalls mid-transfer must not freeze the firmware either.
//
// `mode` is an Arducam CAM_IMAGE_MODE value (int, to avoid every caller
// needing Arducam_Mega.h just for this enum); the bring-up test uses
// CAM_IMAGE_MODE_QVGA (320x240) as a known, fast-to-verify resolution.
CaptureResult captureToFile(const char* path, int mode, uint32_t timeoutMs = 8000);

}  // namespace camera
