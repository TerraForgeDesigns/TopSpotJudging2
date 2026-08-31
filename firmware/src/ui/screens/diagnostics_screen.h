// DIAGNOSTICS — the one screen in this firmware where technical
// vocabulary is allowed (LANGUAGE.md's explicit carve-out). Reachable
// only by a deliberate gesture (long-press on Settings' bottom hint
// label — see settings_screen.cpp) — never a visible button, and
// unreachable through the normal judging flow.
//
// Plain read-only facts, no actions: camera status + last error, SD
// status + free space, WiFi details, both revision numbers, queue depth,
// vehicle data version + entry counts, firmware version, free heap +
// PSRAM, and a log viewer. Every value here is read directly from
// whichever module already owns it — this screen introduces no new
// state of its own beyond the log ring buffer (diag/log.h).
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class DiagnosticsScreen : public Screen {
public:
    const char* title() const override { return "Diagnostics"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
