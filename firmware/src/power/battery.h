// Battery voltage monitoring — BLOCKED pending hardware confirmation.
//
// The CrowPanel 7" (DIS08070H, standard — not Advance-series) has a JST
// battery connector with a charge circuit, but no documented
// battery-voltage ADC pin or divider anywhere in Elecrow's wiki or either
// reference example repo we checked. We are not guessing a GPIO or
// resistor pair for a voltage-sense circuit — a wrong guess here risks
// actual hardware damage (over-range ADC input), not just a bug. See
// DECISIONS.md.
//
// Once you've confirmed a free ADC-capable GPIO and added a divider
// (100k + 100k from BAT+ to GND recommended — see the conversation this
// was flagged in), add PIN_BATTERY_ADC to pins.h and implement this file
// against it. Until then this deliberately fails to compile if anything
// tries to use it, rather than silently reading a floating/wrong pin.
#pragma once

#include "pins.h"

#ifndef PIN_BATTERY_ADC
#error \
    "PIN_BATTERY_ADC is not defined in pins.h. Battery monitoring is blocked pending hardware confirmation -- see this file's header comment and DECISIONS.md. Do not pick a pin here without updating pins.h's reservation table."
#endif

namespace battery {

// Raw ADC millivolts at the sense pin (post-divider).
uint16_t readRawMillivolts();

// Battery pack voltage, correcting for the divider ratio.
float readBatteryVolts();

// Rough percentage estimate for a 1S LiPo (3.0V empty .. 4.2V full).
// Good enough for a UI battery icon, not a fuel gauge.
uint8_t estimatePercent();

}  // namespace battery
