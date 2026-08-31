#include "sync_state.h"

#include <ArduinoJson.h>

#include "sd_card.h"

namespace storage {

namespace {
constexpr const char* SYNC_STATE_PATH = "/sync_state.json";
constexpr size_t MAX_FILE_SIZE = 512;  // four small numbers — this file is never going to be large
}  // namespace

bool loadSyncState(SyncState* out) {
    *out = SyncState{};
    if (!isMounted()) return false;
    if (!fileExists(SYNC_STATE_PATH)) return true;  // first boot ever — defaults stand

    uint8_t buf[MAX_FILE_SIZE + 1];
    int n = readFile(SYNC_STATE_PATH, buf, MAX_FILE_SIZE);
    if (n <= 0) return true;
    buf[n] = '\0';

    StaticJsonDocument<MAX_FILE_SIZE * 2> doc;
    if (deserializeJson(doc, buf, n)) return true;  // malformed — defaults stand, never blocks boot

    out->lastConfigRevisionApplied = doc["last_config_revision_applied"] | 0;
    out->lastDataRevisionApplied = doc["last_data_revision_applied"] | 0;
    out->lastSuccessfulUpdateEpochSeconds = doc["last_successful_update_epoch_seconds"] | 0;
    out->currentRetryIntervalSeconds = doc["current_retry_interval_seconds"] | 180;
    out->totalCars = doc["total_cars"] | 0;
    out->judgedCars = doc["judged_cars"] | 0;
    out->unjudgedCars = doc["unjudged_cars"] | 0;
    out->flaggedConflictCars = doc["flagged_conflict_cars"] | 0;
    return true;
}

bool saveSyncState(const SyncState& state) {
    StaticJsonDocument<MAX_FILE_SIZE * 2> doc;
    doc["last_config_revision_applied"] = state.lastConfigRevisionApplied;
    doc["last_data_revision_applied"] = state.lastDataRevisionApplied;
    doc["last_successful_update_epoch_seconds"] = state.lastSuccessfulUpdateEpochSeconds;
    doc["current_retry_interval_seconds"] = state.currentRetryIntervalSeconds;
    doc["total_cars"] = state.totalCars;
    doc["judged_cars"] = state.judgedCars;
    doc["unjudged_cars"] = state.unjudgedCars;
    doc["flagged_conflict_cars"] = state.flaggedConflictCars;

    char buf[MAX_FILE_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    return writeFileAtomic(SYNC_STATE_PATH, reinterpret_cast<const uint8_t*>(buf), len);
}

}  // namespace storage
