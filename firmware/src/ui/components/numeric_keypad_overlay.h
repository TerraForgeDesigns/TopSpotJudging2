// Modal numeric-only entry overlay — same chrome as text_keyboard.h's
// overlay (field visible above, Cancel leaves the previous value
// untouched, self-deletes after Done/Cancel) but built on
// components::numericKeypad (digits + Backspace only) instead of
// lv_keyboard's number mode, which includes a decimal point — this is
// for fields that must never take punctuation (Year; a manually entered
// Entry Number), not general numeric text.
#pragma once

#include <lvgl.h>

namespace ui::components {

// Same contract as text_keyboard.h's TextInputCallback — `text` is only
// valid for the duration of this call.
using NumericInputCallback = void (*)(void* ctx, const char* text, bool accepted);

// Checked against the digits typed so far (not padded/formatted) whenever
// the overlay would otherwise accept — used for a plausible-range check
// (e.g. Year: 1885 through the show's event year + 1, see
// ui/screens/vehicle_details_screen.cpp). A null validator always passes.
using NumericValidator = bool (*)(const char* text);

// `maxLen` caps how many digits can be typed at all (0 = unbounded).
// `autoAcceptLen` > 0 auto-fires Done the INSTANT that many digits are
// typed AND `validator` accepts them — no extra tap needed (Year: 4
// digits, closes on a valid year). If that length is reached but
// `validator` rejects it, the overlay stays open with Done disabled and
// `invalidHint` shown instead of silently accepting garbage or leaving
// the judge wondering why nothing happened; Done remains available as a
// manual fallback at any point, gated by the same validator check.
// `invalidHint`/`validator` may be null (no validation — Done always
// accepts once something is typed).
lv_obj_t* numericKeypadOverlay(const char* initialText, const char* placeholder, int maxLen, int autoAcceptLen,
                                NumericValidator validator, const char* invalidHint, NumericInputCallback cb, void* ctx);

}  // namespace ui::components
