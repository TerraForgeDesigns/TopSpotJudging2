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

// Numeric-only entry (a typed score correction, ...) — same overlay
// chrome, lv_keyboard in LV_KEYBOARD_MODE_NUMBER instead of text mode. NOT
// used for Year or a manually-typed Entry Number — those must never take
// punctuation, and this mode's map includes a decimal point; see
// components/numeric_keypad_overlay.h for the no-punctuation equivalent.
lv_obj_t* numericKeyboardOverlay(const char* initialText, const char* placeholder, uint32_t maxLen,
                                  TextInputCallback cb, void* ctx);

// The plain-ASCII lowercase keyboard map this file's own overlay uses
// (see text_keyboard.cpp's header comment for why it's hand-rolled rather
// than lv_keyboard's stock maps) — exposed so an INLINE, always-on-screen
// keyboard (e.g. vehicle_selector_list.cpp's live-filter search box,
// which can't use a pop-up overlay the way a single-field edit does) can
// reuse the exact same key layout and event-handling contract instead of
// duplicating it. Pass to lv_keyboard_set_map() with
// LV_KEYBOARD_MODE_USER_1, and wire key presses via keyPressedIntoTextarea
// below — never lv_keyboard's own textarea auto-wiring, which matches
// special keys by LV_SYMBOL_* strings this font-set doesn't have.
const char** plainTextKeyMap();

// The exact key-press handling text_keyboard.cpp's own overlay uses
// internally (Delete/Space handled explicitly, everything else inserted
// literally) — call from an LV_EVENT_VALUE_CHANGED handler on a keyboard
// built with plainTextKeyMap(), passing the target lv_textarea as
// user_data via lv_event_get_user_data(e), exactly as this file's own
// buildOverlay() does.
void keyPressedIntoTextarea(lv_event_t* e);

}  // namespace ui::components
