// AWARD NOMINATIONS — after scoring, before photos. A checklist of every
// judge-chosen Show Award; tap any that apply (none, one, or several),
// then continue. Nothing here is required — see CONTEXT.md: a judge-
// chosen award works by nomination, not scoring, and a judge at a truck
// must not have to answer anything about Best Bike (there's nothing
// truck-specific to filter here; every judge-chosen award is always
// shown, and "none apply" is always a valid answer for all of them).
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class AwardNominationsScreen : public Screen {
public:
    const char* title() const override { return "Award Nominations"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
