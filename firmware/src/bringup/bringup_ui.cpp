// Bring-up test: the LVGL UI foundation — theme, fonts, component
// library, screen manager. See DECISIONS.md and this task's requirement 6
// ("a UI demo screen reachable from a debug menu... judge legibility at
// arm's length outdoors before the real screens exist").
//
// WHAT TO PHYSICALLY VERIFY (none of this was verified on real hardware
// from here — see DECISIONS.md's honest limits on that):
//   1. Clean refresh: no tearing, flicker, or wrong colors on boot, on
//      scroll, or on any button/keypad/grid press (requirement 7).
//   2. Touch-to-visual response feels immediate (roughly under 100ms) —
//      not scientifically measurable without a scope/camera rig, but a
//      "does this feel laggy" check is still meaningful.
//   3. Tap "UI Component Demo" from this debug menu, then every section:
//      confirm every component renders legibly at arm's length, in
//      direct sunlight if possible, in BOTH the Dark and Daylight
//      buttons at the top of that screen.
//   4. Confirm the theme choice survives a reset (power-cycle after
//      picking Daylight — it should still be Daylight on the next boot,
//      proving the SD-backed persistence in ui/theme.cpp actually works
//      against a real card, not just compiles).
//   5. Navigate into the demo screen and back out (Back button, top
//      header) a dozen or so times — this is the leak-prevention
//      screen_manager design's real test; nothing should slow down,
//      stutter more over time, or crash.
#include <Arduino.h>

#include "display/display.h"
#include "storage/sd_card.h"
#include "ui/lvgl_port.h"
#include "ui/screen_manager.h"
#include "ui/screens/debug_menu_screen.h"
#include "ui/theme.h"

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-ui] starting");

    storage::begin();  // best-effort — theme persistence degrades to "always Dark" if this fails, never blocks boot

    if (!ui::lvglInit()) {
        Serial.println("[bringup-ui] FAILED: ui::lvglInit() returned false (display or PSRAM draw buffer)");
        return;
    }
    ui::theme::init();
    ui::screen_manager::init();
    ui::screen_manager::push(ui::screens::DebugMenuScreen::create);

    Serial.println("[bringup-ui] LVGL initialized, debug menu pushed — verify on screen");
}

void loop() { ui::lvglPump(); }
