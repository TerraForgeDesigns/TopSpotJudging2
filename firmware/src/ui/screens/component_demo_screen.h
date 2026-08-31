// Every component in this library, in every state it supports, on one
// scrollable screen — plus a Dark/Daylight toggle right at the top so
// legibility can be judged at arm's length outdoors in both themes
// without leaving the screen. See this task's requirement 6.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class ComponentDemoScreen : public Screen {
public:
    const char* title() const override { return "Component Demo"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
