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
