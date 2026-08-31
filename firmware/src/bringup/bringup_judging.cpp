// Bring-up: the real judging flow, F3 — Home -> Enter Car -> Vehicle
// Details -> Judge Car -> Award Nominations -> Photos -> Review. This is
// where requirement 2's crash-safety design and requirement 5's
// resume-vs-discard decision actually get exercised end to end — see
// firmware/TESTING.md for the physical battery-pull procedure.
//
// WHAT TO PHYSICALLY VERIFY, beyond bringup-ui's general checks:
//   1. Judge a full car: Enter Car -> a real entry number -> Vehicle
//      Details (edit each field) -> Judge Car (both range layouts if you
//      can test at different show sizes) -> Award Nominations -> take
//      both photos (confirm each preview actually shows the photo, not
//      a blank box) -> Review -> Confirm. Home screen's progress number
//      doesn't change (no sync yet, by design — see requirement 6) but
//      nothing should error.
//   2. Try to Confirm on Review with a photo missing (skip Photos by
//      using the header Back, then re-enter and go straight to Photos)
//      — Confirm must refuse and say why, never queue a car with a
//      missing photo (requirement 4).
//   3. Type the SAME entry number again on Enter Car after finishing it
//      — must say it's already been judged, not let you start over.
//   4. TESTING.md's battery-pull procedure.
#include <Arduino.h>
#include <lvgl.h>

#include "camera/camera.h"
#include "display/display.h"
#include "network/wifi_sync.h"
#include "pins.h"
#include "power/backlight.h"
#include "power/battery_monitor.h"
#include "storage/sd_card.h"
#include "storage/vehicle_db.h"
#include "ui/lvgl_port.h"
#include "ui/screen_manager.h"
#include "ui/screens/home_screen.h"
#include "ui/theme.h"

// pins.h (included above) defines PIN_BATTERY_ADC only once a real ADC pin/divider has
// been confirmed (see power/battery.h — deliberately refuses to compile
// without it, a wrong guess risks real hardware damage). This is the
// ONE place that conditional lives — power::battery_monitor::check()
// itself never includes battery.h and stays buildable either way. See
// DECISIONS.md's F7 entry.
#ifdef PIN_BATTERY_ADC
#include "power/battery.h"
#endif

namespace {
void batteryPollTimerCb(lv_timer_t* /*timer*/) {
#ifdef PIN_BATTERY_ADC
    power::battery_monitor::check(battery::estimatePercent());
#else
    power::battery_monitor::check(-1);  // honestly unknown — no fabricated percentage
#endif
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-judging] starting");

    storage::begin();
    storage::vehicle_db::init();  // safe even if vehicle_seed was never flashed — see storage/vehicle_db.h
    Serial.printf("[bringup-judging] vehicle seed database: %s\n",
                   storage::vehicle_db::seedAvailable() ? "loaded" : "not flashed (Other/manual entry still works)");
    camera::begin();  // non-blocking; the Photos screen itself waits out captureToFile()'s own timeout per-shot

    if (!ui::lvglInit()) {
        Serial.println("[bringup-judging] FAILED: ui::lvglInit()");
        return;
    }
    ui::theme::init();
    ui::screen_manager::init();
    network::sync::init();  // after screen_manager::init() — sets up LVGL timers and needs the status bar to exist
    power::backlight::init();  // after lvglInit() — needs LVGL's display/indev already registered
    lv_timer_create(batteryPollTimerCb, 30000, nullptr);  // 30s — cheap either way, but avoids polling an ADC every frame once one exists
    ui::screen_manager::push(ui::screens::HomeScreen::create);

    Serial.println("[bringup-judging] ready");
}

void loop() { ui::lvglPump(); }
