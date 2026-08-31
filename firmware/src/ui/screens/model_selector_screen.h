// MODEL SELECTOR — search/Recently Used/Other picker for Vehicle Details'
// Model field, scoped to judging::current().make. No search box when that
// make has fewer than ~20 models (task's item 5) — see build().
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class ModelSelectorScreen : public Screen {
public:
    const char* title() const override { return "Model"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
