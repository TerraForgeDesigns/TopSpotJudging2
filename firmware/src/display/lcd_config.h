// LovyanGFX device config for the CrowPanel 7" (DIS08070H) RGB parallel LCD
// + GT911 touch. Every pin and timing value here is taken directly from
// Elecrow's own verified working example for this exact board (see
// pins.h's header comment for sourcing) — the porch/pulse-width/polarity
// values in particular are NOT something to guess or "clean up": they come
// from the panel's real timing requirements, not arbitrary style choices.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <driver/i2c.h>  // for I2C_NUM_1 (GT911 touch i2c_port)

#include "pins.h"

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB _bus_instance;
    lgfx::Panel_RGB _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;

    LGFX(void) {
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = 800;
            cfg.memory_height = 480;
            cfg.panel_width = 800;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

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
            cfg.freq_write = 12000000;

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

            _bus_instance.config(cfg);
        }
        _panel_instance.setBus(&_bus_instance);

        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = PIN_LCD_BACKLIGHT;
            _light_instance.config(cfg);
        }
        _panel_instance.light(&_light_instance);

        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = 799;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.pin_int = PIN_TOUCH_INT;
            cfg.pin_rst = PIN_TOUCH_RST;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = I2C_NUM_1;
            cfg.pin_sda = PIN_TOUCH_SDA;
            cfg.pin_scl = PIN_TOUCH_SCL;
            cfg.freq = 400000;
            cfg.i2c_addr = TOUCH_GT911_I2C_ADDR;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
