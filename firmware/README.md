# Top Spot Judging — Handheld Firmware

ESP32-S3 firmware for the judging handheld. See [../CONTEXT.md](../CONTEXT.md) for
the project overview and [../PROTOCOL.md](../PROTOCOL.md) for the sync contract this
firmware will eventually speak. This phase (**F1 — hardware bring-up**) covers the
display, touch, SD, and camera subsystems in isolation; no judging UI yet.

## Hardware

Elecrow CrowPanel ESP32 Display 7" (DIS08070H V3.0), ESP32-S3-WROOM-1-N4R8, 800x480
RGB parallel LCD, GT911 capacitive touch, onboard microSD, Arducam Mega 5MP SPI on a
second, dedicated SPI bus using repurposed UART0/I2S pins. **Every pin is documented,
with sourcing, in [`include/pins.h`](include/pins.h) — read that before touching any
GPIO assignment.** See [../DECISIONS.md](../DECISIONS.md) for the reasoning behind the
two real risks this firmware works around (a third-party library that hardcodes the
global `SPI` object, and an unbounded busy-wait on a missing camera).

## Setup (needs internet once)

```
cd firmware
pio run -e handheld     # downloads the espressif32 platform, toolchains, and
                         # both libraries (LovyanGFX, Arducam_Mega) into .pio/ —
                         # every build after this is fully offline, matching
                         # ../CONTEXT.md's offline-first constraint.
```

All four environments (`bringup-display`, `bringup-sd`, `bringup-camera`, `handheld`)
have already been built successfully once in this repo, so `.pio/` should already be
populated — you shouldn't need to re-download anything.

## Bring-up sequence

Flash and verify these **one at a time, in order** — each is a separate PlatformIO
environment with only one subsystem's code in it, deliberately not integrated, so a
failure points at exactly one thing.

```
pio run -e bringup-display -t upload -t monitor
```

### 1. Display + touch (`bringup-display`)

**Physically verify:**
- Screen lights up, shows "Top Spot Judging -- Display Bring-Up" and four color
  swatches (gold / green / blue / red) — no tearing, flicker, wrong colors, or
  rotation.
- Touch each corner and the center. A gold crosshair should appear exactly where you
  touched, and the serial monitor should print coordinates matching where you
  touched (near (0,0) top-left, (799,479) bottom-right). Swapped/inverted axes point
  at `lcd_config.h`'s touch rotation config, not a wiring problem.

### 2. SD card (`bringup-sd`)

```
pio run -e bringup-sd -t upload -t monitor
```

**Physically verify:** serial log shows `MOUNT: OK`, `WRITE: OK`, `READ: OK, content
matches`, and a free-space number in the right ballpark for your card. A mount
failure with a card inserted usually means the card isn't FAT32, or a solder joint on
SD_CS/MOSI/SCK/MISO (GPIO10/11/12/13) needs a re-check.

### 3. Camera (`bringup-camera`)

```
pio run -e bringup-camera -t upload -t monitor
```

**Physically verify:**
- `SD MOUNT: OK` first (this test captures *to* the SD card).
- Either `CAMERA READY` within a few seconds, or `CAMERA UNAVAILABLE` — printed
  promptly either way. **If the board goes silent after "starting" and never prints
  again, that's the exact hang this architecture exists to prevent — report that back,
  don't assume it's just slow.**
- `CAPTURE: OK` with a plausible byte count, then pull the card and confirm
  `/bringup_photo.jpg` actually opens as a real image on a computer — a byte count
  alone doesn't prove the JPEG data is valid.
- Unplug the camera and re-run: the board should still boot, still mount SD, and
  report `CAMERA UNAVAILABLE` — never hang, never crash, never take SD down with it
  (this is the real test of the two-independent-SPI-buses fix — see `DECISIONS.md`).

### 4. Battery — blocked

Not built yet. `src/power/battery.h` refuses to compile until a real ADC pin is
confirmed — see that file and the open item in `../DECISIONS.md`.

### 5. Integrated (`handheld`)

Once 1–3 pass individually:

```
pio run -e handheld -t upload -t monitor
```

Shows a plain status screen (Display / SD card / Camera, each OK/FAILED) proving all
three subsystems come up together without the "works alone, fails together" bus
conflicts the hardware contract specifically called out. Not the real judging UI yet
— that's a later phase.

## Project layout

```
platformio.ini       environments: bringup-display, bringup-sd, bringup-camera, handheld
include/pins.h        the ONE place every GPIO assignment is documented and sourced
src/main.cpp          integrated target (F1: status screen only)
src/bringup/          isolated single-subsystem test entry points
src/display/          RGB LCD + GT911 touch (LovyanGFX), theme tokens (from DESIGN.md)
src/camera/           Arducam Mega, isolated on its own FreeRTOS task with a timeout
src/storage/          SD card (own dedicated SPI peripheral); pending_queue.h stubbed
src/network/          WiFi sync — stubbed, later phase
src/power/            battery.h blocked pending hardware; sleep.h stubbed, later phase
src/ui/               judging screens — stubbed, later phase
```
