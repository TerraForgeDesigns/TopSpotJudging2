// Update state — what this handheld last successfully applied from Home
// Base, and where the update-retry backoff currently stands. Internal
// bookkeeping only; a judge never sees these numbers or the word
// "revision" — see LANGUAGE.md ("Configuration Revision" / "Show Data
// Revision" -> never displayed; "Synchronization / Sync" -> "Updating" /
// "Up to Date"). The status bar reads this to decide which of those
// words to show, and how long ago "last update" was.
#pragma once

#include <cstdint>

namespace storage {

struct SyncState {
    int lastConfigRevisionApplied = 0;
    int lastDataRevisionApplied = 0;
    uint32_t lastSuccessfulUpdateEpochSeconds = 0;  // 0 = never — the status bar shows "Not Connected" for this
    uint32_t currentRetryIntervalSeconds = 180;      // PROTOCOL.md's periodic trigger default (3 minutes)

    // PROTOCOL.md's sync response `summary` block, as of the last
    // successful update — the HOME screen's "142 of 310 judged" reads
    // this directly rather than computing anything itself (a handheld
    // has no way to know the show-wide total independently).
    int totalCars = 0;
    int judgedCars = 0;
    int unjudgedCars = 0;
    int flaggedConflictCars = 0;
};

bool loadSyncState(SyncState* out);
bool saveSyncState(const SyncState& state);  // atomic write to /sync_state.json

}  // namespace storage
