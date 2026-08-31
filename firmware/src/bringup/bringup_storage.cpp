// Bring-up / smoke test: the local storage layer (settings, show cache,
// sync state, the finished-car queue, drafts) — see storage/*.h and
// firmware/TESTING.md for the real crash-safety procedure this supports.
//
// WHAT TO PHYSICALLY VERIFY:
//   1. Serial log shows every round-trip below as OK.
//   2. Pull the microSD card and inspect on a computer: /settings.txt is
//      plain, readable text; /show.json, /entries.json, /sync_state.json,
//      /queue/*.json, /drafts/*.json all exist and are valid JSON.
//   3. Re-run this test WITHOUT reformatting the card — SETTINGS ROUND
//      TRIP and QUEUE INTEGRITY should reflect what the previous run
//      already wrote (proves persistence survives a reboot, not just a
//      single power-on session).
#include <Arduino.h>

#include "storage/drafts.h"
#include "storage/pending_queue.h"
#include "storage/sd_card.h"
#include "storage/settings.h"
#include "storage/show_data.h"
#include "storage/sync_state.h"

namespace {
void logResult(const char* label, bool ok) { Serial.printf("[bringup-storage] %-28s %s\n", label, ok ? "OK" : "FAILED"); }
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-storage] starting");

    bool sdOk = storage::begin();
    logResult("SD mount", sdOk);
    if (!sdOk) return;

    // --- Settings: plain-text round trip ---
    storage::Settings settings;
    strncpy(settings.handheldLabel, "hh-1", sizeof(settings.handheldLabel));
    strncpy(settings.judgeName, "R. Alvarez", sizeof(settings.judgeName));
    strncpy(settings.wifiSsid, "TopSpotShow", sizeof(settings.wifiSsid));
    strncpy(settings.wifiPassword, "carshow2026", sizeof(settings.wifiPassword));
    strncpy(settings.homeBaseAddress, "192.168.8.100:8000", sizeof(settings.homeBaseAddress));
    settings.theme = storage::ThemeChoice::Daylight;
    bool settingsSaved = storage::saveSettings(settings);
    logResult("settings save", settingsSaved);

    storage::Settings reloaded;
    bool settingsLoaded = storage::loadSettings(&reloaded) &&
                           strcmp(reloaded.handheldLabel, "hh-1") == 0 &&
                           strcmp(reloaded.homeBaseAddress, "192.168.8.100:8000") == 0 &&
                           reloaded.theme == storage::ThemeChoice::Daylight;
    logResult("settings round trip", settingsLoaded);

    // --- Show info + entries ---
    storage::ShowInfo show;
    strncpy(show.showName, "Route 9 Fall Cruise-In", sizeof(show.showName));
    show.scoreRangeMax = 10;
    show.maxScore = 40;
    show.overallImpressionEnabled = true;
    show.categoryCount = 2;
    // Field-by-field, not brace-init-assignment — these structs have
    // default member initializers, which (per this project's own F1
    // lesson, see DECISIONS.md) don't reliably aggregate-initialize
    // under this toolchain's C++ standard level.
    show.categories[0].id = 1;
    strncpy(show.categories[0].name, "Engine", sizeof(show.categories[0].name));
    show.categories[0].sortOrder = 0;
    show.categories[1].id = 2;
    strncpy(show.categories[1].name, "Paint", sizeof(show.categories[1].name));
    show.categories[1].sortOrder = 1;
    show.awardCount = 1;
    show.awards[0].id = 12;
    strncpy(show.awards[0].name, "Best Paint", sizeof(show.awards[0].name));
    logResult("show info save", storage::saveShowInfo(show));

    storage::ShowInfo showReloaded;
    bool showLoaded = storage::loadShowInfo(&showReloaded) && showReloaded.categoryCount == 2 &&
                       strcmp(showReloaded.categories[1].name, "Paint") == 0;
    logResult("show info round trip", showLoaded);

    storage::Entry entries[2];
    strncpy(entries[0].entryNumber, "042", sizeof(entries[0].entryNumber));
    strncpy(entries[0].participant, "Marcus Webb", sizeof(entries[0].participant));
    strncpy(entries[1].entryNumber, "187", sizeof(entries[1].entryNumber));
    logResult("entries save", storage::saveEntries(entries, 2));

    storage::Entry found;
    bool knownFound = storage::findEntry("042", &found) && strcmp(found.participant, "Marcus Webb") == 0;
    logResult("entry lookup (known)", knownFound);
    bool unknownNotFound = !storage::findEntry("999", &found);
    logResult("entry lookup (not in list)", unknownNotFound);

    // --- Sync state ---
    storage::SyncState syncState;
    syncState.lastConfigRevisionApplied = 4;
    syncState.lastDataRevisionApplied = 812;
    syncState.lastSuccessfulUpdateEpochSeconds = 1735000000;
    logResult("sync state save", storage::saveSyncState(syncState));
    storage::SyncState syncReloaded;
    logResult("sync state round trip",
              storage::loadSyncState(&syncReloaded) && syncReloaded.lastDataRevisionApplied == 812);

    // --- Draft (in-progress car) ---
    storage::DraftCar draft;
    strncpy(draft.entryNumber, "042", sizeof(draft.entryNumber));
    draft.furthestStep = storage::DraftStep::Judging;
    draft.scoreRangeMax = 10;
    draft.scoreCount = 1;
    draft.scores[0].categoryId = 1;
    draft.scores[0].points = 8;
    logResult("draft save", storage::saveDraft(draft));
    storage::DraftCar draftReloaded;
    logResult("draft round trip", storage::loadDraft("042", &draftReloaded) &&
                                       draftReloaded.furthestStep == storage::DraftStep::Judging &&
                                       draftReloaded.scores[0].points == 8);
    logResult("draft delete on finish", storage::deleteDraft("042"));
    logResult("draft gone after delete", !storage::hasDraft("042"));

    // --- Finished-car queue (the crash-critical one) ---
    storage::QueuedCar car;
    strncpy(car.entryNumber, "042", sizeof(car.entryNumber));
    strncpy(car.judgeName, "R. Alvarez", sizeof(car.judgeName));
    car.closedAtUptimeMs = millis();
    car.scoreRangeMax = 10;
    car.scoreCount = 2;
    car.scores[0].categoryId = 1;
    car.scores[0].points = 8;
    car.scores[1].categoryId = 2;
    car.scores[1].points = 9;
    car.nominationCount = 1;
    car.nominations[0] = 12;
    logResult("enqueue car", storage::enqueueCar(car));
    logResult("queue count >= 1", storage::countQueued() >= 1);

    storage::QueueIntegrityReport report = storage::checkQueueIntegrity();
    Serial.printf(
        "[bringup-storage] queue integrity: %d file(s), %d valid, %d orphaned .tmp cleaned, %d unreadable\n",
        report.totalFiles, report.validCars, report.orphanedTmpFiles, report.unreadable);
    logResult("queue integrity clean", report.unreadable == 0);

    Serial.println("[bringup-storage] done");
}

void loop() { delay(1000); }
