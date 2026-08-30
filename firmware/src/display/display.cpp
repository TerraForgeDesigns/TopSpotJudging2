#include "display.h"

namespace display {

namespace {
LGFX g_lgfx;
bool g_ready = false;
}  // namespace

bool begin() {
    g_ready = g_lgfx.init();
    if (g_ready) {
        g_lgfx.setBrightness(255);
    }
    return g_ready;
}

bool isReady() { return g_ready; }

LGFX& gfx() { return g_lgfx; }

bool getTouch(int32_t* x, int32_t* y) { return g_lgfx.getTouch(x, y); }

void setBacklight(uint8_t brightness) { g_lgfx.setBrightness(brightness); }

}  // namespace display
