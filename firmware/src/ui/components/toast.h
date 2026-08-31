// A bottom-anchored, auto-dismissing confirmation banner ("Saved.",
// "042 already scored — showing existing scores.") — for something the
// judge should notice but doesn't need to act on, unlike alert_banner's
// in-place notices or modal_confirm's blocking ones.
#pragma once

#include <lvgl.h>

namespace ui::components {

enum class ToastSeverity { Success, Error };

// Shown for `durationMs`, then removed automatically. Built on the top
// layer, so it survives even if the screen underneath changes while it's
// visible.
void showToast(const char* text, ToastSeverity severity, uint32_t durationMs = 2500);

}  // namespace ui::components
