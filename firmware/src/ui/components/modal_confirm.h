// A dimmed full-screen overlay with a centered confirm card — "are you
// sure" for anything consequential (discarding a duplicate score set,
// leaving a screen with unsaved photos, ...). Deletes itself after either
// button; the caller never manages its lifetime.
#pragma once

#include <lvgl.h>

namespace ui::components {

using ModalConfirmCallback = void (*)(void* ctx, bool confirmed);

// `confirmLabel` lets the caller word the affirmative action specifically
// (LANGUAGE.md: plain language over a generic "OK") — e.g. "Discard",
// "Replace Scores". `destructive` renders the confirm button as a
// warning-weight action rather than the ordinary primary gold button.
// Always built on LVGL's top layer — no `parent` parameter, same reason
// as text_keyboard.h's overlays.
lv_obj_t* modalConfirm(const char* title, const char* body, const char* confirmLabel, bool destructive,
                        ModalConfirmCallback cb, void* ctx);

}  // namespace ui::components
