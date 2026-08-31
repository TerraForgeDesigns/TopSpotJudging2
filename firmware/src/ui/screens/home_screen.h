// HOME — the root screen. Judge name / battery / update status live in
// the persistent status bar (built once by screen_manager::init(), not
// rebuilt here — see components/status_bar.h). This screen itself is
// just show progress from the last update, the primary "Judge a Car"
// action, and access to Settings.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class HomeScreen : public Screen {
public:
    const char* title() const override { return "Top Spot Judging"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
