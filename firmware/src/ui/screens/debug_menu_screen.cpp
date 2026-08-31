#include "debug_menu_screen.h"

#include "../components/list_row.h"
#include "component_demo_screen.h"

namespace ui::screens {

namespace {
void openComponentDemo(void* /*ctx*/) { screen_manager::push(ComponentDemoScreen::create); }
}  // namespace

void DebugMenuScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 8, 0);

    components::listRow(content, "UI Component Demo", "every state, both themes", components::RowDot::None,
                         openComponentDemo, nullptr);
}

Screen* DebugMenuScreen::create(void* /*arg*/) { return new DebugMenuScreen(); }

}  // namespace ui::screens
