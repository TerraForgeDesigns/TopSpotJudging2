// ENTER CAR — numeric keypad for the entry number from the paper form,
// looked up in the local cache (see storage/show_data.h). A number not
// yet in the local cache is never an error — see CONTEXT.md: "You can
// still judge it."
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class EnterCarScreen : public Screen {
public:
    const char* title() const override { return "Enter Car"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
