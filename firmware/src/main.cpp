// Top Spot Judging — handheld firmware, integrated target.
//
// This phase (F1, hardware bring-up) intentionally does NOT build the
// judging UI yet — that's a later phase, once display/SD/camera are each
// verified working in isolation via the bringup-* environments (see
// platformio.ini and firmware/README.md). This main.cpp exists to prove
// all three subsystems can be brought up TOGETHER without the
// "works alone, fails together" bus-coupling problem the hardware
// contract specifically called out — it shows a plain status screen, not
// real judging functionality.
#include <Arduino.h>

#include "camera/camera.h"
#include "display/display.h"
#include "display/theme.h"
#include "storage/sd_card.h"

namespace {
constexpr uint32_t CAMERA_INIT_TIMEOUT_MS = 4000;

void drawStatusLine(int y, const char* label, bool ok, bool known = true) {
    using display::gfx;
    gfx().setTextDatum(textdatum_t::top_left);
    gfx().setTextSize(2);
    gfx().setTextColor(theme::TEXT_SECONDARY, theme::INK_900);
    gfx().drawString(label, 20, y);
    uint16_t color = !known ? theme::TEXT_SECONDARY : (ok ? theme::GREEN_500 : theme::RED_500);
    const char* text = !known ? "..." : (ok ? "OK" : "FAILED");
    gfx().setTextColor(color, theme::INK_900);
    gfx().drawString(text, 260, y);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[handheld] Top Spot Judging firmware starting");

    bool displayOk = display::begin();
    bool sdOk = storage::begin();
    camera::begin();  // non-blocking; polled below

    if (displayOk) {
        using display::gfx;
        gfx().fillScreen(theme::INK_900);
        gfx().setTextSize(3);
        gfx().setTextColor(theme::GOLD_500, theme::INK_900);
        gfx().setTextDatum(textdatum_t::top_left);
        gfx().drawString("Top Spot Judging", 20, 20);
        gfx().setTextSize(1);
        gfx().setTextColor(theme::TEXT_SECONDARY, theme::INK_900);
        gfx().drawString("Subsystem status (F1 hardware bring-up)", 20, 60);

        drawStatusLine(100, "Display", true);
        drawStatusLine(130, "SD card", sdOk);
        drawStatusLine(160, "Camera", false, /*known=*/false);
    }

    bool cameraReady = camera::waitReady(CAMERA_INIT_TIMEOUT_MS);
    Serial.printf("[handheld] display=%s sd=%s camera=%s\n", displayOk ? "ok" : "FAILED", sdOk ? "ok" : "FAILED",
                  cameraReady ? "ready" : "unavailable");

    if (displayOk) {
        drawStatusLine(160, "Camera", cameraReady);
    }
}

void loop() { delay(1000); }
