// The root screen for the UI-foundation bring-up target (bringup-ui) —
// not part of the real judging flow. A judge never sees this; it exists
// so every component in the library can be checked for legibility at
// arm's length, outdoors, in both themes, before any real screen is
// built on top of this foundation — see this task's requirement 6.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class DebugMenuScreen : public Screen {
public:
    const char* title() const override { return "Debug Menu"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
