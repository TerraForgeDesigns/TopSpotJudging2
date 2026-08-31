// A full-width banner for something the judge needs to notice before
// continuing. Only two severities — Info (blue) and Critical (red) — no
// gold "warning" variant exists, matching DESIGN.md's hard rule that gold
// is brand/primary-action only and status is always green/blue/red.
#pragma once

#include <lvgl.h>

namespace ui::components {

enum class AlertSeverity { Info, Critical };

lv_obj_t* alertBanner(lv_obj_t* parent, AlertSeverity severity, const char* text);

}  // namespace ui::components
