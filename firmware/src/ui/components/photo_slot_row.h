// A photo status row — label, a colored dot + short status word (Taken /
// Missing, matching the same dot+word shape as every other status
// indicator in this library), and a button to take or retake it. This is
// a STATUS row, not an image preview — decoding and displaying a captured
// JPEG on-device is a real future feature, deliberately out of scope for
// this UI foundation (LV_USE_SJPG/PNG are off in lv_conf.h to keep this
// pass's flash footprint down; see this task's flash report).
#pragma once

#include <lvgl.h>

namespace ui::components {

enum class PhotoSlotState { Missing, Taken };

using PhotoSlotCallback = void (*)(void* ctx);

lv_obj_t* photoSlotRow(lv_obj_t* parent, const char* label, PhotoSlotState state, const char* actionLabel,
                        PhotoSlotCallback cb, void* ctx);

}  // namespace ui::components
