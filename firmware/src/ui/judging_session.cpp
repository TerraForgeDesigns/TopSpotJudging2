#include "judging_session.h"

#include <Arduino.h>
#include <cstring>

#include "network/wifi_sync.h"
#include "storage/pending_queue.h"
#include "storage/settings.h"

namespace ui::judging {

namespace {
bool g_active = false;
storage::DraftCar g_draft;
}  // namespace

bool isActive() { return g_active; }

const char* entryNumber() { return g_draft.entryNumber; }

void start(const char* entry, int scoreRangeMax) {
    if (!storage::loadDraft(entry, &g_draft)) {
        g_draft = storage::DraftCar{};
        strncpy(g_draft.entryNumber, entry, sizeof(g_draft.entryNumber) - 1);
        g_draft.scoreRangeMax = scoreRangeMax;
    }
    g_active = true;
}

storage::DraftCar& current() { return g_draft; }

bool save() {
    if (!g_active) return false;
    return storage::saveDraft(g_draft);
}

bool finish() {
    if (!g_active) return false;
    if (!g_draft.carPhotoSaved || !g_draft.sheetPhotoSaved) return false;  // Review must never allow this anyway

    storage::Settings settings;
    storage::loadSettings(&settings);  // judge name is a per-device setting, not per-car — see settings.h

    storage::QueuedCar car;
    strncpy(car.entryNumber, g_draft.entryNumber, sizeof(car.entryNumber) - 1);
    strncpy(car.judgeName, settings.judgeName, sizeof(car.judgeName) - 1);
    car.closedAtUptimeMs = millis();
    strncpy(car.participant, g_draft.participant, sizeof(car.participant) - 1);
    strncpy(car.year, g_draft.year, sizeof(car.year) - 1);
    strncpy(car.make, g_draft.make, sizeof(car.make) - 1);
    strncpy(car.model, g_draft.model, sizeof(car.model) - 1);
    strncpy(car.vehicleType, g_draft.vehicleType, sizeof(car.vehicleType) - 1);
    car.makeManuallyEntered = g_draft.makeManuallyEntered;
    car.modelManuallyEntered = g_draft.modelManuallyEntered;
    car.scoreRangeMax = g_draft.scoreRangeMax;
    car.scoreCount = g_draft.scoreCount;
    for (int i = 0; i < g_draft.scoreCount; i++) car.scores[i] = g_draft.scores[i];
    car.hasOverallImpression = g_draft.hasOverallImpression;
    car.overallImpression = g_draft.overallImpression;
    car.nominationCount = g_draft.nominationCount;
    for (int i = 0; i < g_draft.nominationCount; i++) car.nominations[i] = g_draft.nominations[i];

    if (!storage::enqueueCar(car)) return false;  // see pending_queue.h — the caller must not treat this as finished

    storage::deleteDraft(g_draft.entryNumber);
    g_active = false;
    network::sync::requestNow();  // trigger (a): a car was just finished — try immediately, ignoring backoff
    return true;
}

void discard() {
    if (!g_active) return;
    storage::deleteDraft(g_draft.entryNumber);
    g_active = false;
}

}  // namespace ui::judging
