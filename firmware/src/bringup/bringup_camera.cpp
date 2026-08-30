// Bring-up test: Arducam Mega, on its own dedicated SPI bus, capturing to
// the onboard SD card (SD is required for this test specifically, since
// "capture a JPEG and write it to SD" is the whole point — see the
// project's hardware contract). This is also the real integration test
// for the two-independent-SPI-buses fix: if SD bring-up passed alone but
// this test corrupts SD or hangs, that's the bus-coupling risk documented
// in camera.h/DECISIONS.md showing up for real.
//
// WHAT TO PHYSICALLY VERIFY:
//   1. Serial monitor at 115200. Confirm "SD MOUNT: OK" first.
//   2. Watch camera init: either "CAMERA READY" within a few seconds, or
//      "CAMERA UNAVAILABLE" — the latter should print promptly (within
//      ~4s, the init timeout below), NOT hang. If the board never prints
//      anything at all after "starting", that's the exact hang scenario
//      this architecture is meant to prevent — worth reporting back.
//   3. On success, confirm "CAPTURE: OK" with a plausible byte count
//      (a few KB to a few hundred KB for QVGA JPEG), then pull the SD
//      card and check /bringup_photo.jpg actually opens as a real JPEG
//      on a computer -- byte count alone doesn't prove valid image data.
//   4. Unplug the camera entirely and re-run: confirm the board still
//      boots, still prints SD status, and reports CAMERA UNAVAILABLE
//      instead of hanging or crashing.
#include <Arduino.h>
#include <Arducam_Mega.h>

#include "camera/camera.h"
#include "storage/sd_card.h"

namespace {
constexpr uint32_t CAMERA_INIT_TIMEOUT_MS = 4000;
constexpr uint32_t CAPTURE_TIMEOUT_MS = 8000;
const char* kPhotoPath = "/bringup_photo.jpg";
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-camera] starting");

    bool sdOk = storage::begin();
    Serial.printf("[bringup-camera] SD MOUNT: %s\n", sdOk ? "OK" : "FAILED");
    if (!sdOk) {
        Serial.println("[bringup-camera] aborting -- capture needs somewhere to write to");
        return;
    }

    camera::begin();
    Serial.println("[bringup-camera] camera init started, waiting...");
    bool ready = camera::waitReady(CAMERA_INIT_TIMEOUT_MS);
    Serial.printf("[bringup-camera] %s\n", ready ? "CAMERA READY" : "CAMERA UNAVAILABLE");
    if (!ready) {
        Serial.println("[bringup-camera] no camera detected/responding -- this is a normal, "
                        "handled condition, not a crash. Photo capture will stay disabled.");
        return;
    }

    camera::CaptureResult result = camera::captureToFile(kPhotoPath, CAM_IMAGE_MODE_QVGA, CAPTURE_TIMEOUT_MS);
    if (result.success) {
        Serial.printf("[bringup-camera] CAPTURE: OK, %lu bytes written to %s\n", (unsigned long)result.bytesWritten,
                      kPhotoPath);
    } else {
        Serial.printf("[bringup-camera] CAPTURE: FAILED -- %s\n", result.error);
    }
}

void loop() { delay(1000); }
