// MAKE SELECTOR — search/Recently Used/Other picker for Vehicle Details'
// Make field, backed by storage::vehicle_db (flash-mapped seed + learned,
// merged) and storage::vehicle_recents. See
// ui/components/vehicle_selector_list.h for the shared layout/behavior
// and DECISIONS.md's F4 entry for the Make->Model auto-advance rule this
// screen's selection handler implements.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class MakeSelectorScreen : public Screen {
public:
    const char* title() const override { return "Make"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
