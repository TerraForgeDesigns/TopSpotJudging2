#include "alert_banner.h"

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

lv_obj_t* alertBanner(lv_obj_t* parent, AlertSeverity severity, const char* text) {
    lv_obj_t* banner = lv_obj_create(parent);
    lv_obj_remove_style_all(banner);
    lv_obj_add_style(banner, severity == AlertSeverity::Critical ? theme::bannerCritical() : theme::bannerInfo(), 0);
    lv_obj_set_style_pad_all(banner, 14, 0);
    lv_obj_set_size(banner, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(banner);
    lv_obj_add_style(label, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(label, &ui_font_plex_400_16, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);

    return banner;
}

}  // namespace ui::components
