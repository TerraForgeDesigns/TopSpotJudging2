#include "scoring_updated_screen.h"

#include <cstdio>

#include "../components/button.h"
#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::screens {

namespace {

int g_newRangeMax = 5;  // set by create() just before build() reads it — see header comment

void onContinue(lv_event_t*) { screen_manager::pop(); }

}  // namespace

void ScoringUpdatedScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 16, 0);

    lv_obj_t* body = lv_label_create(content);
    lv_obj_add_style(body, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(body, &ui_font_plex_400_16, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 480);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);

    char text[256];
    snprintf(text, sizeof(text),
             "This show now uses scores from 1 to %d because more cars were added.\n\n"
             "Scores you already sent have been adjusted automatically.",
             g_newRangeMax);
    lv_label_set_text(body, text);

    lv_obj_t* continueBtn = components::primaryButton(content, "Continue", 320);
    lv_obj_set_style_pad_top(continueBtn, 8, 0);
    lv_obj_add_event_cb(continueBtn, onContinue, LV_EVENT_CLICKED, nullptr);
}

Screen* ScoringUpdatedScreen::create(void* arg) {
    g_newRangeMax = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    return new ScoringUpdatedScreen();
}

}  // namespace ui::screens
