#include "backlight.h"

#include <lvgl.h>

#include "display/display.h"
#include "storage/settings.h"

namespace power::backlight {

namespace {

constexpr uint32_t POLL_INTERVAL_MS = 1000;
constexpr uint8_t BRIGHTNESS_FULL = 255;
// Dim, not off — still readable at a glance, distinct from the Off state
// PWM verification below cares about. Not exposed as a setting (only the
// two TIMEOUTS are, per the task) — this is a fixed visual choice, not a
// hardware fact that needs confirming.
constexpr uint8_t BRIGHTNESS_DIM = 40;
constexpr uint8_t BRIGHTNESS_OFF = 0;

enum class State { Full, Dim, Off };
State g_state = State::Full;

// display::setBacklight(0) writes a true zero PWM duty cycle, not a dim
// floor — verified by reading LovyanGFX's Light_PWM::setBrightness()
// directly (firmware/.pio/libdeps/*/LovyanGFX/src/lgfx/v1/platforms/
// esp32/Light_PWM.cpp): brightness==0 skips its offset/floor calculation
// entirely (`if (brightness) { ...offset math... }`) and writes duty=0
// via ledcWrite() unconditionally otherwise. See DECISIONS.md's F7 entry
// — this is a source-code confirmation, not a hardware measurement; the
// task's own ask for a real meter/scope check is still real work, done
// on physical hardware, not by this comment.
void applyState(State state) {
    if (state == g_state) return;
    g_state = state;
    switch (state) {
        case State::Full: display::setBacklight(BRIGHTNESS_FULL); break;
        case State::Dim: display::setBacklight(BRIGHTNESS_DIM); break;
        case State::Off: display::setBacklight(BRIGHTNESS_OFF); break;
    }
}

void pollTimerCb(lv_timer_t* /*timer*/) {
    storage::Settings settings;
    storage::loadSettings(&settings);

    uint32_t idleMs = lv_disp_get_inactive_time(nullptr);
    uint32_t dimMs = static_cast<uint32_t>(settings.backlightDimSeconds) * 1000;
    uint32_t offMs = static_cast<uint32_t>(settings.backlightOffSeconds) * 1000;

    // Touch is still polled normally in every state here (see this
    // module's header comment) — LVGL resets its own inactivity clock the
    // instant a touch is processed, so a fresh touch already means
    // idleMs is back near 0 by the time this timer next fires; no
    // separate "wake" branch is needed, Full is just whichever state a
    // small idleMs naturally selects below.
    if (idleMs >= offMs) {
        applyState(State::Off);
    } else if (idleMs >= dimMs) {
        applyState(State::Dim);
    } else {
        applyState(State::Full);
    }
}

}  // namespace

void init() { lv_timer_create(pollTimerCb, POLL_INTERVAL_MS, nullptr); }

}  // namespace power::backlight
