// Bring-up test: RGB LCD + GT911 touch, in isolation.
//
// WHAT TO PHYSICALLY VERIFY:
//   1. Screen lights up and shows the "Top Spot Judging" text and four
//      color swatches (gold / green / blue / red) without tearing,
//      flickering, or wrong colors/rotation.
//   2. Touch each of the four corners and the center of the screen in
//      turn. A small crosshair should appear exactly where you touched,
//      and the Serial monitor should print coordinates close to what you
//      expect (e.g. near (0,0) top-left, near (799,479) bottom-right).
//      If X/Y are swapped or inverted, that's a rotation/mapping issue to
//      fix in lcd_config.h's touch cfg, not a wiring problem.
#include <Arduino.h>

#include "display/display.h"
#include "display/theme.h"

using display::gfx;

namespace {
void drawSwatch(int x, int y, int w, int h, uint16_t color, const char* label) {
    gfx().fillRect(x, y, w, h, color);
    gfx().setTextColor(theme::TEXT_ON_GOLD, color);
    gfx().setTextDatum(textdatum_t::middle_center);
    gfx().drawString(label, x + w / 2, y + h / 2);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-display] starting");

    if (!display::begin()) {
        Serial.println("[bringup-display] FAILED: display::begin() returned false");
        return;
    }
    Serial.println("[bringup-display] display initialized OK");

    gfx().fillScreen(theme::INK_900);
    gfx().setTextColor(theme::TEXT_PRIMARY, theme::INK_900);
    gfx().setTextDatum(textdatum_t::top_left);
    gfx().setTextSize(2);
    gfx().drawString("Top Spot Judging -- Display Bring-Up", 20, 20);
    gfx().setTextSize(1);
    gfx().drawString("Touch the screen -- a crosshair should track your finger.", 20, 60);

    int swatchW = 150, swatchH = 80, gap = 20, startX = 20, y = 100;
    drawSwatch(startX + 0 * (swatchW + gap), y, swatchW, swatchH, theme::GOLD_500, "GOLD");
    drawSwatch(startX + 1 * (swatchW + gap), y, swatchW, swatchH, theme::GREEN_500, "GREEN");
    drawSwatch(startX + 2 * (swatchW + gap), y, swatchW, swatchH, theme::BLUE_500, "BLUE");
    drawSwatch(startX + 3 * (swatchW + gap), y, swatchW, swatchH, theme::RED_500, "RED");

    Serial.println("[bringup-display] test pattern drawn -- verify colors/text on screen");
}

void loop() {
    int32_t x, y;
    if (display::getTouch(&x, &y)) {
        Serial.printf("[bringup-display] touch at (%ld, %ld)\n", (long)x, (long)y);
        gfx().fillCircle(x, y, 4, theme::GOLD_500);
    }
    delay(20);
}
