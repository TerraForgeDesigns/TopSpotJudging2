// Full-screen text/number entry overlays, built on LVGL's own
// lv_keyboard + lv_textarea widgets (per this task's explicit instruction
// — not a hand-rolled keyboard). Landscape-aware: sized for 800x480, the
// keyboard docked to the bottom, textarea + Done/Cancel above it, never
// the portrait stacking lv_keyboard defaults to.
//
// Cancel always leaves the field's previous value untouched — the
// callback receives accepted=false and the caller must ignore `text` in
// that case (see LANGUAGE.md's error/action standard: a person must never
// lose what they had by backing out of an edit). The overlay deletes
// itself after Done or Cancel — the caller never needs to manage its
// lifetime.
#pragma once

#include <lvgl.h>

namespace ui::components {

// `text` is only valid for the duration of this call — it points into
// the textarea's own buffer, which is freed immediately after (the
// overlay deletes itself right after invoking the callback). Copy it if
// you need it beyond this call.
using TextInputCallback = void (*)(void* ctx, const char* text, bool accepted);

// General text entry (participant name, make, model, ...). `maxLen` caps
// input length (0 = lv_textarea's own default, effectively unbounded).
// Always built on LVGL's top layer (see .cpp) — there is no `parent`
// parameter, since the overlay must outlive and sit above whichever
// screen is currently on the navigation stack.
lv_obj_t* textKeyboardOverlay(const char* initialText, const char* placeholder, uint32_t maxLen,
                               TextInputCallback cb, void* ctx);

// Numeric-only entry (year, a typed score correction, ...) — same overlay
// chrome, lv_keyboard in LV_KEYBOARD_MODE_NUMBER instead of text mode.
lv_obj_t* numericKeyboardOverlay(const char* initialText, const char* placeholder, uint32_t maxLen,
                                  TextInputCallback cb, void* ctx);

}  // namespace ui::components
