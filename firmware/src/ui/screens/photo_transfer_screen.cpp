#include "photo_transfer_screen.h"

#include <cstdio>
#include <cstring>

#include "../components/alert_banner.h"
#include "../components/button.h"
#include "../components/card.h"
#include "../components/list_row.h"
#include "../components/modal_confirm.h"
#include "../components/toast.h"
#include "../fonts/fonts.h"
#include "../theme.h"
#include "network/photo_upload.h"
#include "storage/photo_state.h"
#include "storage/sd_card.h"

namespace ui::screens {

namespace {

// True once "Transfer Photos" has unmounted the card for physical
// removal — the screen shows ONLY the safe-to-remove banner and the
// "Card Reinserted" action while this is set, per the task's "never let
// a card be pulled with buffered writes outstanding" (everything else on
// this screen needs the card mounted anyway, so hiding it avoids showing
// stale zeroed-out data rather than the real reason it's empty).
bool g_cardRemovedMode = false;

lv_timer_t* g_progressTimer = nullptr;
lv_obj_t* g_progressLabel = nullptr;

void onTransferPhotos(lv_event_t*) {
    storage::unmount();
    g_cardRemovedMode = true;
    screen_manager::refresh();
}

void onCardReinserted(lv_event_t*) {
    storage::begin();
    g_cardRemovedMode = false;
    screen_manager::refresh();
}

void progressTimerCb(lv_timer_t* /*timer*/) {
    if (g_progressLabel == nullptr) return;
    if (network::photo_upload::isRunning()) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Sending photo %d of %d", network::photo_upload::currentIndex(),
                 network::photo_upload::total());
        lv_label_set_text(g_progressLabel, buf);
    } else {
        screen_manager::refresh();  // a batch just finished (or was never running) — show the real, current counts
    }
}

void onSendOverWifi(lv_event_t*) {
    network::photo_upload::startTransfer();
    screen_manager::refresh();
}

void onClearConfirmed(void* /*ctx*/, bool confirmed) {
    if (!confirmed) return;
    if (storage::clearAllPhotos()) {
        components::showToast("Photos cleared.", components::ToastSeverity::Success);
    } else {
        components::showToast("Photos cannot be cleared until every photo has been sent.",
                               components::ToastSeverity::Error, 3500);
    }
    screen_manager::refresh();
}

void onClearPhotos(lv_event_t*) {
    int total = storage::countPhotos();
    char body[128];
    snprintf(body, sizeof(body),
             "This permanently deletes all %d photo%s on this card. This cannot be undone. Only do this before the "
             "next show.",
             total, total == 1 ? "" : "s");
    components::modalConfirm("Clear Photos?", body, "Delete All Photos", /*destructive=*/true, onClearConfirmed,
                              nullptr);
}

}  // namespace

void PhotoTransferScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 12, 0);

    if (g_cardRemovedMode) {
        components::alertBanner(content, components::AlertSeverity::Info, "Safe to remove the memory card.");
        lv_obj_t* reinsertBtn = components::primaryButton(content, "Card Reinserted", 320);
        lv_obj_add_event_cb(reinsertBtn, onCardReinserted, LV_EVENT_CLICKED, nullptr);
        return;
    }

    // Heap-allocated, sized to what's actually on the card — a fixed
    // MAX_LISTED_PHOTOS-sized STATIC array here would permanently reserve
    // that much RAM whether or not a card is even present; ESP32-S3's
    // 320KB SRAM doesn't have room for firmware-wide "just in case"
    // reservations like that (this exact mistake overflowed the DRAM
    // segment at link time during F6's own build verification — see
    // DECISIONS.md).
    int photoCount = storage::countPhotos();
    int untransferred = storage::countUntransferred();
    auto* photos = new storage::PhotoRecord[photoCount > 0 ? photoCount : 1];
    photoCount = storage::listPhotos(photos, photoCount);

    char headerBuf[64];
    snprintf(headerBuf, sizeof(headerBuf), photoCount == 1 ? "1 photo on this card" : "%d photos on this card",
              photoCount);
    lv_obj_t* header = lv_label_create(content);
    lv_obj_add_style(header, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(header, &ui_font_archivo_700_23, 0);
    lv_label_set_text(header, headerBuf);

    if (photoCount > 0) {
        lv_obj_t* list = components::card(content);
        lv_obj_set_height(list, 180);
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        // Grouped by car, one row per entry number — "Car + Sheet" /
        // "Car only" / "Sheet only" reads plainer than two separate rows
        // per car for a list that can run into the hundreds.
        auto* seen = new char[photoCount][8];
        int seenCount = 0;
        for (int i = 0; i < photoCount; i++) {
            bool already = false;
            for (int j = 0; j < seenCount; j++) {
                if (strcmp(seen[j], photos[i].entryNumber) == 0) {
                    already = true;
                    break;
                }
            }
            if (already) continue;
            strncpy(seen[seenCount++], photos[i].entryNumber, 7);

            bool hasCar = false, hasSheet = false, allSent = true;
            for (int j = 0; j < photoCount; j++) {
                if (strcmp(photos[j].entryNumber, photos[i].entryNumber) != 0) continue;
                if (strcmp(photos[j].type, "car") == 0) hasCar = true;
                if (strcmp(photos[j].type, "sheet") == 0) hasSheet = true;
                if (!photos[j].transferred) allSent = false;
            }
            const char* value = hasCar && hasSheet ? "Car + Sheet" : hasCar ? "Car only" : "Sheet only";
            components::listRow(list, photos[i].entryNumber, value, allSent ? components::RowDot::Good : components::RowDot::Pending,
                                 nullptr, nullptr);
        }
        delete[] seen;
    }
    delete[] photos;

    bool uploadRunning = network::photo_upload::isRunning();

    lv_obj_t* transferBtn = components::primaryButton(content, "Transfer Photos", 320);
    lv_obj_add_event_cb(transferBtn, onTransferPhotos, LV_EVENT_CLICKED, nullptr);
    components::setEnabled(transferBtn, photoCount > 0 && !uploadRunning);

    lv_obj_t* wifiNote = lv_label_create(content);
    lv_obj_add_style(wifiNote, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(wifiNote, &ui_font_plex_400_14, 0);
    lv_label_set_long_mode(wifiNote, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wifiNote, LV_PCT(100));
    lv_label_set_text(wifiNote, "This uses more power - best done with the device connected to power.");

    lv_obj_t* wifiBtn = components::secondaryButton(content, "Send Photos over Wi-Fi", 320);
    lv_obj_add_event_cb(wifiBtn, onSendOverWifi, LV_EVENT_CLICKED, nullptr);
    components::setEnabled(wifiBtn, untransferred > 0 && !uploadRunning);

    if (uploadRunning) {
        g_progressLabel = lv_label_create(content);
        lv_obj_add_style(g_progressLabel, theme::textPrimary(), 0);
        lv_obj_set_style_text_font(g_progressLabel, &ui_font_plex_500_19, 0);
        lv_label_set_text(g_progressLabel, "Sending photo...");
        if (g_progressTimer == nullptr) g_progressTimer = lv_timer_create(progressTimerCb, 200, nullptr);
    } else {
        g_progressLabel = nullptr;
    }

    lv_obj_t* clearBtn = components::secondaryButton(content, "Clear Photos", 320);
    lv_obj_add_event_cb(clearBtn, onClearPhotos, LV_EVENT_CLICKED, nullptr);
    components::setEnabled(clearBtn, storage::allTransferred() && !uploadRunning);
}

void PhotoTransferScreen::teardown() {
    if (g_progressTimer != nullptr) {
        lv_timer_del(g_progressTimer);
        g_progressTimer = nullptr;
    }
    g_progressLabel = nullptr;
}

Screen* PhotoTransferScreen::create(void* /*arg*/) { return new PhotoTransferScreen(); }

}  // namespace ui::screens
