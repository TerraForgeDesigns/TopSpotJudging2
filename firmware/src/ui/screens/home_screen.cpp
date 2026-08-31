#include "home_screen.h"

#include <cstdio>

#include "../components/button.h"
#include "../components/card.h"
#include "../fonts/fonts.h"
#include "../theme.h"
#include "enter_car_screen.h"
#include "settings_screen.h"
#include "storage/sync_state.h"

namespace ui::screens {

namespace {

void onJudgeACar(lv_event_t*) { screen_manager::push(EnterCarScreen::create); }
void onSettings(lv_event_t*) { screen_manager::push(SettingsScreen::create); }

}  // namespace

void HomeScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 20, 0);

    storage::SyncState syncState;
    storage::loadSyncState(&syncState);

    lv_obj_t* progressCard = components::card(content);
    lv_obj_set_width(progressCard, 480);
    lv_obj_set_style_pad_all(progressCard, 24, 0);

    lv_obj_t* progressLabel = lv_label_create(progressCard);
    lv_obj_add_style(progressLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(progressLabel, &ui_font_plex_500_19, 0);
    lv_label_set_text(progressLabel, "SHOW PROGRESS");

    lv_obj_t* bigNumber = lv_label_create(progressCard);
    lv_obj_add_style(bigNumber, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(bigNumber, &ui_font_archivo_800_46, 0);
    char buf[48];
    if (syncState.totalCars > 0) {
        snprintf(buf, sizeof(buf), "%d of %d judged", syncState.judgedCars, syncState.totalCars);
    } else {
        // Never a fabricated "0 of 0" — this handheld hasn't received a
        // show-wide count yet (no update has landed since boot).
        snprintf(buf, sizeof(buf), "Not updated yet");
    }
    lv_label_set_text(bigNumber, buf);

    if (syncState.flaggedConflictCars > 0) {
        lv_obj_t* conflictLabel = lv_label_create(progressCard);
        lv_obj_add_style(conflictLabel, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(conflictLabel, &ui_font_plex_400_16, 0);
        snprintf(buf, sizeof(buf), "%d flagged for the host to resolve", syncState.flaggedConflictCars);
        lv_label_set_text(conflictLabel, buf);
    }

    lv_obj_t* judgeBtn = components::primaryButton(content, "Judge a Car", 320);
    lv_obj_add_event_cb(judgeBtn, onJudgeACar, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* settingsBtn = components::secondaryButton(content, "Settings", 320);
    lv_obj_add_event_cb(settingsBtn, onSettings, LV_EVENT_CLICKED, nullptr);
}

Screen* HomeScreen::create(void* /*arg*/) { return new HomeScreen(); }

}  // namespace ui::screens
