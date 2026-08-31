#include "card.h"

#include "../theme.h"

namespace ui::components {

lv_obj_t* card(lv_obj_t* parent) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, theme::card(), 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    return c;
}

}  // namespace ui::components
