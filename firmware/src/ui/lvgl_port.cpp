#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "display/display.h"

namespace ui {

namespace {

// 800 x DRAW_BUF_LINES x 2 (double-buffered) x 2 bytes/px (RGB565) —
// LVGL's documented guideline is roughly 1/10th of the screen's rows per
// buffer as the performance/memory sweet spot; at 48 rows that's
// 800*48*2 = 76,800 bytes per buffer, ~150KB total, trivial against 8MB of
// PSRAM. Double-buffered (not single) so LVGL can render the next region
// into buf2 while LovyanGFX is still pushing buf1's DMA transfer out to
// the panel — see requirement 7's no-tearing-on-scroll budget. If real
// hardware doesn't hit the ~100ms touch-to-visual budget, this is the
// first knob to try (larger buffer = fewer, bigger flush calls).
constexpr uint16_t DISPLAY_HOR_RES = 800;
constexpr uint16_t DISPLAY_VER_RES = 480;
constexpr uint16_t DRAW_BUF_LINES = 48;
constexpr size_t DRAW_BUF_PIXELS = DISPLAY_HOR_RES * DRAW_BUF_LINES;

lv_disp_draw_buf_t g_drawBuf;
lv_color_t* g_buf1 = nullptr;
lv_color_t* g_buf2 = nullptr;
lv_disp_drv_t g_dispDrv;
lv_indev_drv_t g_indevDrv;
uint32_t g_lastTickMs = 0;

void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    // pushImage() blocks until the transfer completes — see this file's
    // header comment on why a hand-rolled async/DMA pipeline wasn't
    // attempted here without hardware to verify it against; LovyanGFX's
    // own pushImage already DMA-accelerates the write to an RGB bus
    // internally where the panel driver supports it.
    display::gfx().pushImage(area->x1, area->y1, w, h, reinterpret_cast<uint16_t*>(color_p));
    lv_disp_flush_ready(drv);
}

void touchReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    int32_t x = 0, y = 0;
    if (display::getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

}  // namespace

bool lvglInit() {
    if (!display::isReady() && !display::begin()) {
        return false;
    }

    lv_init();

    g_buf1 = static_cast<lv_color_t*>(
        heap_caps_malloc(DRAW_BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    g_buf2 = static_cast<lv_color_t*>(
        heap_caps_malloc(DRAW_BUF_PIXELS * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    if (g_buf1 == nullptr || g_buf2 == nullptr) {
        return false;  // PSRAM allocation failed — never fall back to internal SRAM silently
    }
    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, DRAW_BUF_PIXELS);

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = DISPLAY_HOR_RES;
    g_dispDrv.ver_res = DISPLAY_VER_RES;
    g_dispDrv.flush_cb = flushCb;
    g_dispDrv.draw_buf = &g_drawBuf;
    lv_disp_drv_register(&g_dispDrv);

    lv_indev_drv_init(&g_indevDrv);
    g_indevDrv.type = LV_INDEV_TYPE_POINTER;
    g_indevDrv.read_cb = touchReadCb;
    lv_indev_drv_register(&g_indevDrv);

    g_lastTickMs = millis();
    return true;
}

void lvglPump() {
    // LV_TICK_CUSTOM is off (see lv_conf.h — Arduino.h can't be pulled
    // into the plain-C lv_tick.c) — driven by hand here instead, off
    // loop()'s own millis(), which is simpler and avoids any ISR-safety
    // question around calling into LVGL from a timer callback.
    uint32_t now = millis();
    lv_tick_inc(now - g_lastTickMs);
    g_lastTickMs = now;
    lv_timer_handler();
}

}  // namespace ui
