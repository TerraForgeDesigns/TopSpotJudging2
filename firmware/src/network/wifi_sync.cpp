#include "wifi_sync.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>

#include "storage/pending_queue.h"
#include "storage/settings.h"
#include "storage/show_data.h"
#include "storage/sync_state.h"
#include "storage/vehicle_db.h"
#include "ui/components/status_bar.h"
#include "ui/components/toast.h"
#include "ui/screen_manager.h"
#include "ui/screens/scoring_updated_screen.h"

namespace network::sync {

namespace {

constexpr uint32_t TASK_STACK = 8192;              // matches camera.cpp's capture task size
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;  // bounded, so a flaky AP can't hang the task indefinitely
constexpr uint32_t HTTP_TIMEOUT_MS = 8000;
constexpr uint32_t POLL_TIMER_MS = 150;
constexpr uint32_t MAX_RETRY_INTERVAL_SECONDS = 900;  // PROTOCOL.md: cap at 15 minutes
constexpr int MAX_SYNC_BATCH = 64;  // defensive cap on cars sent in one attempt — see wifi_sync.h

lv_timer_t* g_periodicTimer = nullptr;
lv_timer_t* g_pollTimer = nullptr;
volatile bool g_running = false;

// Shared between the main thread and the background task. The task ONLY
// reads the Input fields and ONLY writes the Output fields — see
// wifi_sync.h's header comment for why storage/LVGL never happen inside
// the task itself. A single static instance is enough: g_running gates
// against a second attempt starting before this one's Output fields have
// been consumed.
struct Attempt {
    // Input — set by buildRequestBody() (main thread) before the task starts.
    char ssid[32] = "";
    char password[64] = "";
    char url[128] = "";
    char* requestBody = nullptr;
    size_t requestLen = 0;
    bool isPeriodic = false;

    // Output — set by syncTask() (background task).
    volatile bool done = false;
    bool scanFound = false;
    bool ok = false;  // true only on a clean HTTP 200 with a body actually read
    char* responseBody = nullptr;
    size_t responseLen = 0;
};

Attempt g_attempt;

// ---- request body construction (main thread only — reads storage) -----

bool buildRequestBody() {
    storage::Settings settings;
    storage::loadSettings(&settings);
    storage::SyncState syncState;
    storage::loadSyncState(&syncState);

    storage::ShowInfo show;
    storage::loadShowInfo(&show);

    storage::QueuedCar cars[MAX_SYNC_BATCH];
    int carCount = storage::listQueuedCars(cars, MAX_SYNC_BATCH);

    DynamicJsonDocument doc(static_cast<size_t>(carCount) * 700 + 2048);
    doc["handheld_id"] = settings.handheldLabel;
    doc["show_id"] = show.showId > 0 ? show.showId : 0;
    doc["config_revision"] = syncState.lastConfigRevisionApplied;
    doc["data_revision"] = syncState.lastDataRevisionApplied;
    // No battery ADC pin identified yet (storage/settings.h, DECISIONS.md)
    // — sent as an honest null, never a fabricated percentage.
    doc["battery_pct"] = nullptr;

    JsonArray subs = doc.createNestedArray("submissions");
    for (int i = 0; i < carCount; i++) {
        const storage::QueuedCar& car = cars[i];
        JsonObject s = subs.createNestedObject();
        s["entry_number"] = car.entryNumber;
        if (car.judgeName[0] != '\0') s["judge_name"] = car.judgeName;
        s["closed_at_uptime_ms"] = car.closedAtUptimeMs;
        s["participant"] = car.participant;
        s["year"] = car.year;
        s["make"] = car.make;
        s["model"] = car.model;
        s["vehicle_type"] = car.vehicleType;
        s["make_manually_entered"] = car.makeManuallyEntered;
        s["model_manually_entered"] = car.modelManuallyEntered;
        s["score_range_max"] = car.scoreRangeMax;  // mandatory on every submission — see PROTOCOL.md

        JsonArray scores = s.createNestedArray("scores");
        for (int j = 0; j < car.scoreCount; j++) {
            JsonObject sc = scores.createNestedObject();
            sc["category_id"] = car.scores[j].categoryId;
            sc["points"] = car.scores[j].points;
        }
        if (car.hasOverallImpression) {
            s["overall_impression"] = car.overallImpression;
        } else {
            s["overall_impression"] = nullptr;
        }
        JsonArray noms = s.createNestedArray("nominations");
        for (int j = 0; j < car.nominationCount; j++) noms.add(car.nominations[j]);
    }

    size_t needed = measureJson(doc) + 1;
    g_attempt.requestBody = static_cast<char*>(malloc(needed));
    if (g_attempt.requestBody == nullptr) return false;
    g_attempt.requestLen = serializeJson(doc, g_attempt.requestBody, needed);

    strncpy(g_attempt.ssid, settings.wifiSsid, sizeof(g_attempt.ssid) - 1);
    strncpy(g_attempt.password, settings.wifiPassword, sizeof(g_attempt.password) - 1);
    snprintf(g_attempt.url, sizeof(g_attempt.url), "http://%s/api/v1/sync", settings.homeBaseAddress);
    return true;
}

// ---- background task: network I/O only, nothing else ------------------

void syncTask(void* /*unused*/) {
    g_attempt.scanFound = false;
    g_attempt.ok = false;
    g_attempt.responseBody = nullptr;
    g_attempt.responseLen = 0;

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == g_attempt.ssid) {
            g_attempt.scanFound = true;
            break;
        }
    }
    WiFi.scanDelete();

    if (g_attempt.scanFound) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_attempt.ssid, g_attempt.password);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            WiFiClient client;
            HTTPClient http;
            if (http.begin(client, g_attempt.url)) {
                http.addHeader("Content-Type", "application/json");
                http.setTimeout(HTTP_TIMEOUT_MS);
                int code = http.POST(reinterpret_cast<uint8_t*>(g_attempt.requestBody), g_attempt.requestLen);
                if (code == 200) {
                    String resp = http.getString();
                    g_attempt.responseLen = resp.length();
                    g_attempt.responseBody = static_cast<char*>(malloc(g_attempt.responseLen + 1));
                    if (g_attempt.responseBody != nullptr) {
                        memcpy(g_attempt.responseBody, resp.c_str(), g_attempt.responseLen + 1);
                        g_attempt.ok = true;
                    }
                }
                http.end();
            }
        }
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);  // CONTEXT.md: radio off outside a sync window — including a scan-only miss

    free(g_attempt.requestBody);
    g_attempt.requestBody = nullptr;
    g_attempt.done = true;  // last write — the poll timer only acts once this is true
    vTaskDelete(nullptr);
}

// ---- applying a result (main thread only) ------------------------------

void rescheduleBackoff(bool hit) {
    storage::SyncState state;
    storage::loadSyncState(&state);
    storage::Settings settings;
    storage::loadSettings(&settings);

    if (hit) {
        state.currentRetryIntervalSeconds = static_cast<uint32_t>(settings.syncIntervalSeconds);
    } else {
        uint32_t doubled = state.currentRetryIntervalSeconds * 2;
        state.currentRetryIntervalSeconds = doubled > MAX_RETRY_INTERVAL_SECONDS ? MAX_RETRY_INTERVAL_SECONDS : doubled;
    }
    storage::saveSyncState(state);
    if (g_periodicTimer != nullptr) {
        lv_timer_set_period(g_periodicTimer, state.currentRetryIntervalSeconds * 1000);
        lv_timer_reset(g_periodicTimer);
    }
}

void applyMiss() {
    ui::components::setConnState(ui::components::ConnState::NotConnected);
    if (g_attempt.scanFound) {
        // Reached the AP but Home Base itself didn't respond — the
        // judge-facing full-sentence form (LANGUAGE.md's established
        // phrasing), distinct from the status bar's own terse pill word.
        ui::components::showToast("Home Base Not Connected", ui::components::ToastSeverity::Error);
    }
    // Task point: "Never apply backoff to a just-finished-car trigger" —
    // and, by the same reasoning, Update Now. Only the periodic trigger's
    // own misses ever double the interval.
    if (g_attempt.isPeriodic) rescheduleBackoff(false);
}

void applyHit() {
    // SyncState BEFORE this attempt touches it — specifically so
    // "was this the very first configuration this handheld ever applied"
    // can be answered from a value nothing below has overwritten yet.
    storage::SyncState stateBefore;
    storage::loadSyncState(&stateBefore);
    bool hadConfigBefore = stateBefore.lastConfigRevisionApplied > 0;

    DynamicJsonDocument doc(g_attempt.responseLen * 2 + 2048);
    DeserializationError err = deserializeJson(doc, g_attempt.responseBody, g_attempt.responseLen);
    if (err) {
        // A malformed body is exactly the "connection dropped mid-reply"
        // case PROTOCOL.md's idempotency section is built for — treat it
        // as a miss. Nothing was acknowledged, so nothing was removed;
        // the same submissions retry cleanly next attempt.
        applyMiss();
        return;
    }

    // --- server_time: sets the device clock ------------------------------
    const char* serverTime = doc["server_time"] | "";
    struct tm tmv = {};
    if (strptime(serverTime, "%Y-%m-%dT%H:%M:%S", &tmv) != nullptr) {
        time_t t = mktime(&tmv);
        struct timeval tv = {t, 0};
        settimeofday(&tv, nullptr);
    }

    // --- configuration: apply, then decide which notice (if any) --------
    storage::ShowInfo before;
    storage::loadShowInfo(&before);

    JsonVariantConst config = doc["configuration"];
    if (!config.isNull()) {
        storage::ShowInfo after = before;
        after.showId = config["show_id"] | before.showId;  // may be absent from Home Base today — see DECISIONS.md
        strncpy(after.showName, config["show_name"] | before.showName, sizeof(after.showName) - 1);
        strncpy(after.eventDate, config["event_date"] | before.eventDate, sizeof(after.eventDate) - 1);
        after.scoreRangeMax = config["score_range_max"] | before.scoreRangeMax;
        after.maxScore = config["max_score"] | before.maxScore;
        after.overallImpressionEnabled = config["overall_impression_enabled"] | before.overallImpressionEnabled;

        after.categoryCount = 0;
        for (JsonVariantConst c : config["categories"].as<JsonArrayConst>()) {
            if (after.categoryCount >= storage::MAX_CATEGORIES) break;
            storage::Category& cat = after.categories[after.categoryCount++];
            cat.id = c["id"] | 0;
            cat.sortOrder = c["sort_order"] | 0;
            strncpy(cat.name, c["name"] | "", sizeof(cat.name) - 1);
        }
        after.awardCount = 0;
        for (JsonVariantConst a : config["judge_chosen_awards"].as<JsonArrayConst>()) {
            if (after.awardCount >= storage::MAX_AWARDS) break;
            storage::JudgeChosenAward& award = after.awards[after.awardCount++];
            award.id = a["id"] | 0;
            strncpy(award.name, a["name"] | "", sizeof(award.name) - 1);
        }

        storage::saveShowInfo(after);

        // First-ever configuration this handheld has applied is normal
        // setup, not a "change" — neither notice fires for it.
        if (hadConfigBefore) {
            if (after.scoreRangeMax != before.scoreRangeMax) {
                ui::screen_manager::push(ui::screens::ScoringUpdatedScreen::create,
                                          reinterpret_cast<void*>(static_cast<intptr_t>(after.scoreRangeMax)));
            } else {
                ui::components::showToast("Show Setup Updated", ui::components::ToastSeverity::Success);
            }
        }
    }

    // --- cars[]: replace FULL rosters, merge same-show deltas ---
    JsonArrayConst carsArr = doc["cars"].as<JsonArrayConst>();
    bool fullSnapshot = strcmp(doc["sync_mode"] | "", "FULL") == 0;
    int deltaCount = carsArr.size();
    if (deltaCount > 0 || fullSnapshot) {
        auto* delta = new storage::Entry[deltaCount];
        int i = 0;
        for (JsonVariantConst c : carsArr) {
            storage::Entry& e = delta[i++];
            strncpy(e.entryNumber, c["entry_number"] | "", sizeof(e.entryNumber) - 1);
            strncpy(e.participant, c["participant"] | "", sizeof(e.participant) - 1);
            strncpy(e.year, c["year"] | "", sizeof(e.year) - 1);
            strncpy(e.make, c["make"] | "", sizeof(e.make) - 1);
            strncpy(e.model, c["model"] | "", sizeof(e.model) - 1);
            strncpy(e.vehicleType, c["vehicle_type"] | "", sizeof(e.vehicleType) - 1);
        }
        if (fullSnapshot) {
            storage::saveEntries(delta, deltaCount);
        } else {
            storage::mergeEntries(delta, deltaCount);
        }
        delete[] delta;
    }

    // --- vehicle_additions: always [] from Home Base today (DECISIONS.md)
    // — still wired for real, so it starts working the moment the server
    // side exists, with no firmware change needed then.
    for (JsonVariantConst v : doc["vehicle_additions"].as<JsonArrayConst>()) {
        const char* make = v["make"] | "";
        const char* model = v["model"] | "";
        if (make[0] != '\0' && model[0] != '\0') storage::vehicle_db::saveLearnedVehicle(make, model);
    }

    // --- results[]: only an explicit acknowledgement removes a queued car
    for (JsonVariantConst r : doc["results"].as<JsonArrayConst>()) {
        const char* entryNumber = r["entry_number"] | "";
        const char* status = r["status"] | "";
        if (entryNumber[0] == '\0') continue;
        if (strcmp(status, "accepted") == 0 || strcmp(status, "already_recorded") == 0) {
            storage::removeQueuedCarByEntryNumber(entryNumber);
        } else if (strcmp(status, "flagged_duplicate") == 0) {
            // The judge cannot fix this on the device — a small
            // non-blocking note, never an interruption (task point 2).
            storage::removeQueuedCarByEntryNumber(entryNumber);
            char msg[96];
            snprintf(msg, sizeof(msg), "Entry %s was already judged by another handheld.", entryNumber);
            ui::components::showToast(msg, ui::components::ToastSeverity::Error);
        }
        // "error" is deliberately left queued — not an explicit
        // acknowledgement per the task's own framing. See DECISIONS.md's
        // F5 entry for why this is flagged rather than resolved here.
    }

    // --- summary + revision watermarks -----------------------------------
    JsonVariantConst summary = doc["summary"];
    storage::SyncState state;
    storage::loadSyncState(&state);
    state.totalCars = summary["total_cars"] | state.totalCars;
    state.judgedCars = summary["judged"] | state.judgedCars;
    state.unjudgedCars = summary["unjudged"] | state.unjudgedCars;
    state.flaggedConflictCars = summary["flagged_conflict"] | state.flaggedConflictCars;
    state.lastConfigRevisionApplied = doc["config_revision"] | state.lastConfigRevisionApplied;
    state.lastDataRevisionApplied = doc["data_revision"] | state.lastDataRevisionApplied;
    state.lastSuccessfulUpdateEpochSeconds = static_cast<uint32_t>(time(nullptr));
    storage::saveSyncState(state);

    rescheduleBackoff(/*hit=*/true);  // resets the interval to base regardless of which trigger fired
    ui::components::setConnState(ui::components::ConnState::UpToDate);
}

// ---- timers --------------------------------------------------------------

void startAttempt(bool periodic) {
    if (g_running) return;  // an attempt is already in flight — dropped, not queued; it'll run again soon regardless
    if (!buildRequestBody()) return;
    g_attempt.isPeriodic = periodic;
    g_running = true;
    xTaskCreatePinnedToCore(syncTask, "wifi_sync", TASK_STACK, nullptr, 1, nullptr, 1);
}

void periodicTimerCb(lv_timer_t* /*timer*/) { startAttempt(/*periodic=*/true); }

void pollTimerCb(lv_timer_t* /*timer*/) {
    if (!g_attempt.done) return;
    g_attempt.done = false;

    if (g_attempt.ok) {
        applyHit();
    } else {
        applyMiss();
    }
    if (g_attempt.responseBody != nullptr) {
        free(g_attempt.responseBody);
        g_attempt.responseBody = nullptr;
    }
    g_running = false;
}

}  // namespace

void init() {
    storage::SyncState state;
    storage::loadSyncState(&state);
    g_periodicTimer = lv_timer_create(periodicTimerCb, state.currentRetryIntervalSeconds * 1000, nullptr);
    g_pollTimer = lv_timer_create(pollTimerCb, POLL_TIMER_MS, nullptr);
}

void requestNow() { startAttempt(/*periodic=*/false); }

}  // namespace network::sync
