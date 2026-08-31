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
// NOTE: one secondary source lists GPIO38 as "Touch INT," which would
// conflict with Camera CS below — see that section's PRE-SOLDER GATE
// block for the required continuity check and the fallback pin.
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
// (FSPI, the default global `SPI` object), on repurposed I2S pins at
// audio amplifier U11. UART0 is NOT used for this — see the RESERVED
// block below — this was the correction that fixed the original design's
// loss of the serial console; see DECISIONS.md's "Decided" entry on the
// reversal and its struck Open item for the full story.
// ---------------------------------------------------------------------------
// GPIO17/18 are this board's I2S SDIN/LRCLK (audio amplifier U11) — Top
// Spot Judging has no audio requirement, so I2S is intentionally never
// initialized and these nets are repurposed (a wired hardware
// modification at U11, not a connector) as camera MOSI/MISO. Both were
// already unused before this change — see the I2S reminder block below.
#define PIN_CAMERA_MOSI 17  // was I2S SDIN
#define PIN_CAMERA_MISO 18  // was I2S LRCLK
// GPIO42 is this board's I2S BCLK (same amplifier, U11) — unchanged from
// the original design, repurposed the same way as MOSI/MISO above.
#define PIN_CAMERA_SCK 42
// GPIO38, exposed on the board's "GPIO_D" connector. UNCONFIRMED — see the
// PRE-SOLDER GATE block immediately below before wiring this one.
#define PIN_CAMERA_CS 38

// ---------------------------------------------------------------------------
// PRE-SOLDER GATE — Camera CS (GPIO38) vs. Touch INT: unresolved, not
// guessed around. One secondary source (an ESPHome community PR for this
// board) lists GPIO38 as GT911 "Touch INT," which would conflict with its
// assignment as Camera CS above. The verified WORKING reference example
// initializes GT911 with INT=-1 (unused, pure I2C polling) successfully,
// so firmware proceeds on GPIO38 as Camera CS — but this is unconfirmed
// against a schematic. REQUIRED before soldering the camera CS wire:
//   Multimeter continuity check, GT911 INT pad -> GPIO38.
//   - No continuity: GPIO38 is genuinely free. Camera CS as defined above
//     is correct. Nothing to change.
//   - Continuity found: GPIO38 is Touch INT, not available for the
//     camera. Change PIN_CAMERA_CS below to the alternate candidate —
//     this is deliberately the only edit that line needs.
// See firmware/TESTING.md's Pre-Solder Checklist for the full procedure
// (this check plus the U11 tap points for MOSI/MISO/SCK above), and
// DECISIONS.md's Open section for why this is a gate, not a guess.
//
// ALTERNATE CS CANDIDATE, if GPIO38 is ruled out: GPIO44. Verified to be
// the only genuinely unclaimed GPIO left on this module once the pins
// above are accounted for — GPIO0-21 and 38-48 are all otherwise spoken
// for (LCD, touch, SD, camera, UART0 below), and GPIO26-37 are internally
// committed to this ESP32-S3-WROOM-1-N4R8's quad flash + octal PSRAM
// (qio_opi, see platformio.ini), not available as GPIO at all. Using
// GPIO44 here is NOT free: it's the second of the two UART0 pins the
// correction above just recovered, so falling back to it reintroduces
// this board's serial-console loss — worth knowing before committing to
// it, not a clean substitute for a genuinely free pin, because there
// isn't one.
// #define PIN_CAMERA_CS 44  // <- swap in ONLY if GPIO38 shows continuity to GT911 INT

// ---------------------------------------------------------------------------
// UART0 — RESERVED for the serial console / USB programming path via the
// CrowPanel's onboard CH340 bridge chip. Nothing else may claim GPIO43/44.
// ---------------------------------------------------------------------------
// RX=44, TX=43. This is the board's ONLY programming/serial-console path —
// GPIO19/20 (the ESP32-S3's fixed native USB pins) are permanently wired
// to GT911 touch I2C on this board, so there is no native-USB fallback if
// UART0 is lost. The original pin map repurposed these two pins for the
// camera and accepted losing this path permanently once the camera was
// soldered on; that decision was REVERSED (see DECISIONS.md) once it was
// clear that cost recurs on every reflash of every handheld for the life
// of the system, not just once during bring-up. Camera SPI now lives
// entirely on the I2S pins above instead. Do not reassign GPIO43/44 to
// anything — including the Camera CS fallback above, unless that fallback
// is actually needed, in which case the console is deliberately traded
// away again as a last resort, not by accident.
#define PIN_UART0_TX 43  // reserved — do not reassign
#define PIN_UART0_RX 44  // reserved — do not reassign (see the CS fallback note above)

// I2S pins are listed here ONLY as a reminder of what NOT to touch — GPIO42,
// 17, and 18 are now camera SCK/MOSI/MISO, so I2S audio must never be
// initialized on this board.
// #define PIN_I2S_LRCLK 18   // now PIN_CAMERA_MISO — do not use for I2S
// #define PIN_I2S_BCLK  42   // now PIN_CAMERA_SCK — do not use for I2S
// #define PIN_I2S_SDIN  17   // now PIN_CAMERA_MOSI — do not use for I2S

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
