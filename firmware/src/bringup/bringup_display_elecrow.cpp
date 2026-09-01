// Bring-up test: Elecrow V3.0 patched RGB bus + GT911 touch, isolated.
//
// Purpose: answer one hardware question only:
// Does Elecrow's double-buffered, VSYNC-synchronized RGB path eliminate the
// 24 MHz corruption seen when GT911 touch is polled?
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <driver/i2c.h>

#include "pins.h"

namespace {
constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 480;
constexpr uint32_t PCLK_HZ = 24000000;

constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t C_BG = RGB565(8, 10, 16);
constexpr uint16_t C_PANEL = RGB565(22, 28, 40);
constexpr uint16_t C_WHITE = RGB565(245, 247, 250);
constexpr uint16_t C_DIM = RGB565(115, 124, 139);
constexpr uint16_t C_GOLD = RGB565(245, 185, 66);
constexpr uint16_t C_GREEN = RGB565(25, 195, 125);
constexpr uint16_t C_BLUE = RGB565(56, 140, 255);
constexpr uint16_t C_RED = RGB565(245, 78, 78);
constexpr uint16_t C_MAGENTA = RGB565(236, 72, 210);
constexpr uint16_t C_CYAN = RGB565(35, 220, 235);

class ElecrowLGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB bus;
    lgfx::Panel_RGB panel;
    lgfx::Light_PWM light;
    lgfx::Touch_GT911 touch;

    ElecrowLGFX() {
        {
            auto cfg = panel.config();
            cfg.memory_width = SCREEN_W;
            cfg.memory_height = SCREEN_H;
            cfg.panel_width = SCREEN_W;
            cfg.panel_height = SCREEN_H;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            panel.config(cfg);
        }

        {
            auto cfg = bus.config();
            cfg.panel = &panel;
            cfg.pin_d0 = (gpio_num_t)PIN_LCD_B0;
            cfg.pin_d1 = (gpio_num_t)PIN_LCD_B1;
            cfg.pin_d2 = (gpio_num_t)PIN_LCD_B2;
            cfg.pin_d3 = (gpio_num_t)PIN_LCD_B3;
            cfg.pin_d4 = (gpio_num_t)PIN_LCD_B4;
            cfg.pin_d5 = (gpio_num_t)PIN_LCD_G0;
            cfg.pin_d6 = (gpio_num_t)PIN_LCD_G1;
            cfg.pin_d7 = (gpio_num_t)PIN_LCD_G2;
            cfg.pin_d8 = (gpio_num_t)PIN_LCD_G3;
            cfg.pin_d9 = (gpio_num_t)PIN_LCD_G4;
            cfg.pin_d10 = (gpio_num_t)PIN_LCD_G5;
            cfg.pin_d11 = (gpio_num_t)PIN_LCD_R0;
            cfg.pin_d12 = (gpio_num_t)PIN_LCD_R1;
            cfg.pin_d13 = (gpio_num_t)PIN_LCD_R2;
            cfg.pin_d14 = (gpio_num_t)PIN_LCD_R3;
            cfg.pin_d15 = (gpio_num_t)PIN_LCD_R4;
            cfg.pin_henable = (gpio_num_t)PIN_LCD_DE;
            cfg.pin_vsync = (gpio_num_t)PIN_LCD_VSYNC;
            cfg.pin_hsync = (gpio_num_t)PIN_LCD_HSYNC;
            cfg.pin_pclk = (gpio_num_t)PIN_LCD_PCLK;
            cfg.freq_write = PCLK_HZ;
            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch = 40;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 1;
            cfg.vsync_pulse_width = 31;
            cfg.vsync_back_porch = 13;
            cfg.pclk_active_neg = 1;
            cfg.de_idle_high = 0;
            cfg.pclk_idle_high = 0;
            bus.config(cfg);
        }
        panel.setBus(&bus);

        {
            auto cfg = light.config();
            cfg.pin_bl = PIN_LCD_BACKLIGHT;
            light.config(cfg);
        }
        panel.light(&light);

        {
            auto cfg = touch.config();
            cfg.x_min = 0;
            cfg.x_max = SCREEN_W - 1;
            cfg.y_min = 0;
            cfg.y_max = SCREEN_H - 1;
            cfg.pin_int = PIN_TOUCH_INT;
            cfg.pin_rst = PIN_TOUCH_RST;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = I2C_NUM_1;
            cfg.pin_sda = PIN_TOUCH_SDA;
            cfg.pin_scl = PIN_TOUCH_SCL;
            cfg.freq = 400000;
            cfg.i2c_addr = TOUCH_GT911_I2C_ADDR;
            touch.config(cfg);
            panel.setTouch(&touch);
        }

        setPanel(&panel);
    }
};

ElecrowLGFX lcd;
LGFX_Sprite canvas(&lcd);
uint8_t* frame_buffers[2] = {nullptr, nullptr};
uint8_t active_buffer = 0;
uint32_t touch_count = 0;

void drawStaticPattern() {
    canvas.fillScreen(C_BG);
    canvas.fillRect(0, 0, SCREEN_W, 6, C_GOLD);
    canvas.fillRect(0, SCREEN_H - 6, SCREEN_W, 6, C_CYAN);
    canvas.fillRect(0, 0, 6, SCREEN_H, C_GREEN);
    canvas.fillRect(SCREEN_W - 6, 0, 6, SCREEN_H, C_RED);

    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(C_WHITE, C_BG);
    canvas.setTextSize(2);
    canvas.drawString("Elecrow V3.0 RGB 24 MHz VSYNC Diagnostic", 24, 22);
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM, C_BG);
    canvas.drawString("Touch continuously. Watch for shimmer, horizontal jumps, duplication, tearing, or RGB corruption.", 24, 56);

    const int y = 92;
    canvas.fillRect(24, y, 172, 74, C_GOLD);
    canvas.fillRect(216, y, 172, 74, C_GREEN);
    canvas.fillRect(408, y, 172, 74, C_BLUE);
    canvas.fillRect(600, y, 172, 74, C_RED);
    canvas.setTextColor(C_BG);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextSize(2);
    canvas.drawString("GOLD", 110, y + 37);
    canvas.drawString("GREEN", 302, y + 37);
    canvas.drawString("BLUE", 494, y + 37);
    canvas.drawString("RED", 686, y + 37);

    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);
    for (int row = 0; row < 10; ++row) {
        uint16_t color = (row % 2 == 0) ? C_PANEL : C_DIM;
        canvas.drawFastHLine(24, 205 + row * 20, 748, color);
    }
    for (int col = 0; col < 16; ++col) {
        uint16_t color = (col % 2 == 0) ? C_BLUE : C_MAGENTA;
        canvas.drawFastVLine(36 + col * 46, 190, 230, color);
    }
    canvas.drawLine(24, 190, 772, 420, C_GOLD);
    canvas.drawLine(772, 190, 24, 420, C_CYAN);

    canvas.setTextColor(C_WHITE, C_BG);
    canvas.drawString("Touch trail area", 24, 438);
    canvas.drawString("Serial prints every sampled point", 566, 438);
}

void selectBuffer(uint8_t index) {
    canvas.setBuffer(frame_buffers[index], SCREEN_W, SCREEN_H, 16);
}

bool present(uint8_t index) {
    if (!lcd.bus.presentFrameBuffer(frame_buffers[index], 50)) {
        Serial.println("[bringup-display-elecrow] VSYNC present timeout");
        return false;
    }
    active_buffer = index;
    return true;
}

void drawTouchPoint(int32_t x, int32_t y) {
    const uint8_t next = active_buffer ^ 1;
    memcpy(frame_buffers[next], frame_buffers[active_buffer], SCREEN_W * SCREEN_H * 2);
    selectBuffer(next);
    const uint16_t color = (touch_count % 2 == 0) ? C_GOLD : C_CYAN;
    canvas.fillCircle(x, y, 7, color);
    canvas.drawCircle(x, y, 14, C_WHITE);
    canvas.drawFastHLine(x - 22, y, 45, C_MAGENTA);
    canvas.drawFastVLine(x, y - 22, 45, C_MAGENTA);
    present(next);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-display-elecrow] starting");
    Serial.println("[bringup-display-elecrow] Elecrow patched RGB bus, 2 framebuffers, 24 MHz");

    if (!psramFound()) {
        Serial.println("[bringup-display-elecrow] FAILED: PSRAM not found");
        return;
    }

    if (!lcd.init()) {
        Serial.println("[bringup-display-elecrow] FAILED: lcd.init() returned false");
        return;
    }
    lcd.setBrightness(255);

    frame_buffers[0] = lcd.bus.getFrameBuffer(0);
    frame_buffers[1] = lcd.bus.getFrameBuffer(1);
    Serial.printf("[bringup-display-elecrow] frame buffers: %p, %p\n", frame_buffers[0], frame_buffers[1]);
    if (frame_buffers[0] == nullptr || frame_buffers[1] == nullptr) {
        Serial.println("[bringup-display-elecrow] FAILED: RGB double framebuffers unavailable");
        return;
    }

    for (uint8_t i = 0; i < 2; ++i) {
        selectBuffer(i);
        drawStaticPattern();
    }
    present(0);
    Serial.printf("[bringup-display-elecrow] GT911 I2C addr 0x%02X on SDA=%d SCL=%d\n",
                  TOUCH_GT911_I2C_ADDR, PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    Serial.println("[bringup-display-elecrow] ready; drag/tap to stress touch polling");
}

void loop() {
    int32_t x = 0;
    int32_t y = 0;
    if (lcd.getTouch(&x, &y)) {
        ++touch_count;
        Serial.printf("[bringup-display-elecrow] touch %lu at (%ld, %ld)\n",
                      (unsigned long)touch_count, (long)x, (long)y);
        drawTouchPoint(x, y);
    } else {
        lcd.bus.waitVSync(50);
    }
    delay(20);
}
