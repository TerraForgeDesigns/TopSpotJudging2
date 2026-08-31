#include "diagnostics_screen.h"

#include <Esp.h>
#include <WiFi.h>
#include <cstdio>
#include <esp_heap_caps.h>

#include "../components/card.h"
#include "../components/list_row.h"
#include "../fonts/fonts.h"
#include "../theme.h"
#include "camera/camera.h"
#include "diag/log.h"
#include "storage/pending_queue.h"
#include "storage/sd_card.h"
#include "storage/show_data.h"
#include "storage/sync_state.h"
#include "storage/vehicle_db.h"

namespace ui::screens {

namespace {

const char* cameraStateName(camera::CameraState state) {
    switch (state) {
        case camera::CameraState::UNINITIALIZED: return "Uninitialized";
        case camera::CameraState::INITIALIZING: return "Initializing";
        case camera::CameraState::READY: return "Ready";
        case camera::CameraState::UNAVAILABLE: return "Unavailable";
    }
    return "Unknown";
}

void sectionLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_add_style(label, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(label, &ui_font_plex_500_19, 0);
    lv_obj_set_style_pad_top(label, 8, 0);
    lv_label_set_text(label, text);
}

void fact(lv_obj_t* card, const char* label, const char* value) {
    components::listRow(card, label, value, components::RowDot::None, nullptr, nullptr);
}

void factInt(lv_obj_t* card, const char* label, long value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%ld", value);
    fact(card, label, buf);
}

}  // namespace

void DiagnosticsScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 4, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    // --- Camera ---
    sectionLabel(content, "CAMERA");
    lv_obj_t* cameraCard = components::card(content);
    fact(cameraCard, "Status", cameraStateName(camera::getState()));
    fact(cameraCard, "Last error", camera::lastError()[0] != '\0' ? camera::lastError() : "None");

    // --- SD card ---
    sectionLabel(content, "SD CARD");
    lv_obj_t* sdCard = components::card(content);
    storage::SdInfo sdInfo = storage::getInfo();
    fact(sdCard, "Mounted", sdInfo.mounted ? "Yes" : "No");
    if (sdInfo.mounted) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%llu MB", sdInfo.freeBytes() / (1024ULL * 1024ULL));
        fact(sdCard, "Free space", buf);
        snprintf(buf, sizeof(buf), "%llu MB", sdInfo.totalBytes / (1024ULL * 1024ULL));
        fact(sdCard, "Total capacity", buf);
    }

    // --- WiFi ---
    sectionLabel(content, "WI-FI");
    lv_obj_t* wifiCard = components::card(content);
    if (WiFi.status() == WL_CONNECTED) {
        fact(wifiCard, "Status", "Connected");
        fact(wifiCard, "SSID", WiFi.SSID().c_str());
        fact(wifiCard, "IP address", WiFi.localIP().toString().c_str());
        char rssiBuf[16];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", WiFi.RSSI());
        fact(wifiCard, "Signal", rssiBuf);
    } else {
        // Radio is deliberately off outside a sync window (CONTEXT.md) —
        // this is the expected state most of the time, not a fault.
        fact(wifiCard, "Status", "Radio off (idle between sync attempts)");
    }

    // --- Sync state ---
    sectionLabel(content, "SYNC");
    lv_obj_t* syncCard = components::card(content);
    storage::SyncState syncState;
    storage::loadSyncState(&syncState);
    factInt(syncCard, "Config revision applied", syncState.lastConfigRevisionApplied);
    factInt(syncCard, "Data revision applied", syncState.lastDataRevisionApplied);
    factInt(syncCard, "Queue depth", storage::countQueued());

    // --- Vehicle data ---
    sectionLabel(content, "VEHICLE DATA");
    lv_obj_t* vehicleCard = components::card(content);
    uint16_t seedVersion = 0;
    uint32_t makeCount = 0, modelCount = 0;
    if (storage::vehicle_db::getSeedInfo(&seedVersion, &makeCount, &modelCount)) {
        factInt(vehicleCard, "Seed version", seedVersion);
        factInt(vehicleCard, "Makes", makeCount);
        factInt(vehicleCard, "Models", modelCount);
    } else {
        fact(vehicleCard, "Seed database", "Not loaded");
    }
    factInt(vehicleCard, "Cached entries", storage::countEntries());

    // --- Firmware ---
    sectionLabel(content, "FIRMWARE");
    lv_obj_t* fwCard = components::card(content);
    fact(fwCard, "Version", FIRMWARE_VERSION);
    factInt(fwCard, "Free heap (bytes)", ESP.getFreeHeap());
    factInt(fwCard, "Free PSRAM (bytes)", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // --- Log viewer ---
    sectionLabel(content, "RECENT LOG");
    lv_obj_t* logCard = components::card(content);
    static char logBuf[4096];
    diag::snapshot(logBuf, sizeof(logBuf));
    lv_obj_t* logLabel = lv_label_create(logCard);
    lv_obj_add_style(logLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(logLabel, &ui_font_plex_400_14, 0);
    lv_label_set_long_mode(logLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(logLabel, LV_PCT(100));
    lv_label_set_text(logLabel, logBuf[0] != '\0' ? logBuf : "Nothing logged yet this session.");
}

Screen* DiagnosticsScreen::create(void* /*arg*/) { return new DiagnosticsScreen(); }

}  // namespace ui::screens
