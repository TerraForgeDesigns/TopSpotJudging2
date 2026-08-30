// Top Spot Judging — handheld pin map
//
// Hardware: Elecrow CrowPanel ESP32 Display 7" (DIS08070H V3.0)
//           ESP32-S3-WROOM-1-N4R8, 800x480 RGB parallel LCD, GT911 touch,
//           Arducam Mega 5MP SPI (dedicated second SPI bus), onboard microSD.
//
// EVERY pin below was verified, not guessed, against:
//   - Elecrow's own DIS08070H wiki page
//   - Two independent working example sketches from the community
//     reference repo Elecrow directs users to for this exact board
//     (github.com/Bentlybro/CrowPanel-7.0-HMI-ESP32-Screen-by-Elecrow-...)
//   - The project's own hardware contract conversation (camera + SD wiring,
//     which is a deliberate custom modification specific to this unit, not
//     documented anywhere else)
// See DECISIONS.md for the full sourcing writeup and the two real risks
// this file's design works around (Arducam_Mega's hardcoded use of the
// global `SPI` object, and its unbounded busy-wait on a missing camera).
//
// DO NOT reassign any of these without updating this file, DECISIONS.md,
// and re-verifying against hardware — this board's pin usage is unusually
// tight (two repurposed interfaces) and got that way through a careful,
// documented negotiation. Silently "fixing" one of these is how you get a
// board that works alone and fails when everything is wired together.

#pragma once

// ---------------------------------------------------------------------------
// RGB parallel LCD (NOT a SPI display — do not add SPI TFT driver code here)
// ---------------------------------------------------------------------------
// 800x480, EK9716BD3-class RGB driver. Timing parameters (porches, pulse
// widths, clock polarity) live in src/display/lcd_config.h alongside the
// LovyanGFX panel config, sourced from the same verified example.
#define PIN_LCD_B0 15
#define PIN_LCD_B1 7
#define PIN_LCD_B2 6
#define PIN_LCD_B3 5
#define PIN_LCD_B4 4
#define PIN_LCD_G0 9
#define PIN_LCD_G1 46
#define PIN_LCD_G2 3
#define PIN_LCD_G3 8
#define PIN_LCD_G4 16
#define PIN_LCD_G5 1
#define PIN_LCD_R0 14
#define PIN_LCD_R1 21
#define PIN_LCD_R2 47
#define PIN_LCD_R3 48
#define PIN_LCD_R4 45
#define PIN_LCD_HSYNC 39
#define PIN_LCD_VSYNC 40
#define PIN_LCD_DE 41
#define PIN_LCD_PCLK 0
#define PIN_LCD_BACKLIGHT 2  // PWM

// ---------------------------------------------------------------------------
// Touch — GT911 capacitive, I2C. RESERVED: never reassign to camera/anything
// else, per the hardware contract.
// ---------------------------------------------------------------------------
#define PIN_TOUCH_SDA 19
#define PIN_TOUCH_SCL 20
#define TOUCH_GT911_I2C_ADDR 0x14
// The working reference example runs GT911 with no dedicated interrupt or
// reset GPIO (both -1 / unused) — pure I2C polling. We do the same.
// NOTE: one secondary source (an ESPHome community PR for this board)
// lists GPIO38 as "Touch INT," which would conflict with Camera CS below.
// The verified WORKING example code does not need or use a touch INT pin
// at all, so we're proceeding on that basis — but this is worth a 30-second
// continuity check (GT911 INT pad to GPIO38) before soldering the camera on.
#define PIN_TOUCH_INT (-1)
#define PIN_TOUCH_RST (-1)

// ---------------------------------------------------------------------------
// Onboard microSD — its own dedicated hardware SPI peripheral (HSPI).
// RESERVED exclusively for SD; never share with the camera.
// ---------------------------------------------------------------------------
#define PIN_SD_CS 10
#define PIN_SD_MOSI 11
#define PIN_SD_SCK 12
#define PIN_SD_MISO 13

// ---------------------------------------------------------------------------
// Arducam Mega 5MP SPI — a SECOND, independent hardware SPI peripheral
// (FSPI, the default global `SPI` object), on repurposed UART0 + I2S pins.
// ---------------------------------------------------------------------------
// GPIO43/44 are this board's UART0 (RX=44/TX=43, i.e. the normal USB-serial
// programming path via the CrowPanel's onboard UART bridge chip). Normal
// handheld operation doesn't need UART0, so these become camera
// MOSI/MISO. IMPORTANT CONSEQUENCE: once the camera is soldered here, this
// board's serial console/programming path is gone — see platformio.ini's
// comment on this. Develop and flash over UART0 BEFORE the camera is
// permanently soldered on.
#define PIN_CAMERA_MOSI 43
#define PIN_CAMERA_MISO 44
// GPIO42 is this board's I2S BCLK (audio amplifier U11). Top Spot Judging
// has no audio requirement, so I2S is intentionally never initialized and
// this net is repurposed (via a wired hardware modification at U11, not a
// connector) as camera SCK.
#define PIN_CAMERA_SCK 42
// GPIO38, exposed on the board's "GPIO_D" connector.
#define PIN_CAMERA_CS 38

// I2S pins are listed here ONLY as a reminder of what NOT to touch — GPIO42
// is now camera SCK, so I2S audio must never be initialized on this board.
// #define PIN_I2S_LRCLK 18   // unused — do not initialize I2S
// #define PIN_I2S_BCLK  42   // now PIN_CAMERA_SCK — do not use for I2S
// #define PIN_I2S_SDIN  17   // unused — do not initialize I2S

// ---------------------------------------------------------------------------
// Battery voltage monitoring — BLOCKED, see DECISIONS.md.
// ---------------------------------------------------------------------------
// This board (standard CrowPanel 7", not Advance-series) has a JST battery
// connector with a charge circuit, but NO documented battery-voltage ADC
// pin or divider anywhere in Elecrow's docs or either reference example
// repo. We are not guessing one. src/power/battery.h refuses to compile
// until PIN_BATTERY_ADC is defined here, once a free ADC-capable GPIO and
// divider are confirmed.
// #define PIN_BATTERY_ADC <pending>
