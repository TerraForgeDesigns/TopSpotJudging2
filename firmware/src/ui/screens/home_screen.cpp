#include "home_screen.h"

#include <cstdio>
#include <ctime>

#include "../components/alert_banner.h"
#include "../components/button.h"
#include "../components/card.h"
#include "../fonts/fonts.h"
#include "../theme.h"
#include "enter_car_screen.h"
#include "network/wifi_sync.h"
#include "settings_screen.h"
#include "storage/pending_queue.h"
#include "storage/sync_state.h"

namespace ui::screens {

namespace {

// Task's own wording: "If nothing has been sent for more than 20
// minutes, make it prominent." Not tied to the periodic timer's own
// (much shorter, configurable) interval — this is about how long it's
// actually been since anything reached Home Base, regardless of how many
// silent scan-misses happened in between.
constexpr uint32_t STALE_WARNING_SECONDS = 20 * 60;

void onJudgeACar(lv_event_t*) { screen_manager::push(EnterCarScreen::create); }
void onSettings(lv_event_t*) { screen_manager::push(SettingsScreen::create); }
void onUpdateNow(lv_event_t*) { network::sync::requestNow(); }  // trigger (c)

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

    int queued = storage::countQueued();
    if (queued > 0) {
        lv_obj_t* queuedLabel = lv_label_create(progressCard);
        lv_obj_add_style(queuedLabel, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(queuedLabel, &ui_font_plex_400_16, 0);
        snprintf(buf, sizeof(buf), queued == 1 ? "1 car waiting to send" : "%d cars waiting to send", queued);
        lv_label_set_text(queuedLabel, buf);
    }

    // Task's own ordering: reassurance first, action second — the
    // judge's real worry ("did I lose my work?") answered before telling
    // them what to do about it.
    bool everSynced = syncState.lastSuccessfulUpdateEpochSeconds > 0;
    uint32_t ageSeconds = everSynced ? static_cast<uint32_t>(time(nullptr)) - syncState.lastSuccessfulUpdateEpochSeconds
                                      : 0;
    if (!everSynced || ageSeconds > STALE_WARNING_SECONDS) {
        components::alertBanner(content, components::AlertSeverity::Critical,
                                 "Your scores are saved on this device. Walk toward Home Base to send them.");
    }

    lv_obj_t* judgeBtn = components::primaryButton(content, "Judge a Car", 320);
    lv_obj_add_event_cb(judgeBtn, onJudgeACar, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* updateBtn = components::secondaryButton(content, "Update Now", 320);
    lv_obj_add_event_cb(updateBtn, onUpdateNow, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* settingsBtn = components::secondaryButton(content, "Settings", 320);
    lv_obj_add_event_cb(settingsBtn, onSettings, LV_EVENT_CLICKED, nullptr);
}

Screen* HomeScreen::create(void* /*arg*/) { return new HomeScreen(); }

}  // namespace ui::screens
