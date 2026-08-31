// In-progress cars — a judge who starts scoring a car and then walks
// away (low battery, called over to something else, distracted) before
// reaching Review must not lose that work. See DECISIONS.md's F3 entry
// for the resume-vs-discard decision this module implements: progress is
// saved automatically and silently at every screen transition within the
// judging flow, and picking the same entry number back up on the ENTER
// CAR screen resumes it — never a forced "discard or keep?" prompt,
// which is itself a way to lose work if a tired judge taps the wrong one.
//
// One draft per entry number, at /drafts/{entry_number}.json, atomic
// writes throughout — the exact same crash-safety approach as
// pending_queue.h, just for work that isn't finished yet. Deleted the
// moment the car is actually finished (moved into the real queue) or the
// judge explicitly discards it (see components/modal_confirm.h's usage
// on the relevant screen).
#pragma once

#include <cstdint>

#include "pending_queue.h"  // reuses QueuedScore/count constants — a draft is "a QueuedCar that isn't finished yet"

namespace storage {

enum class DraftStep {
    VehicleDetails,
    Judging,
    Nominations,
    Photos,
    Review,
};

struct DraftCar {
    char entryNumber[8] = "";
    DraftStep furthestStep = DraftStep::VehicleDetails;
    // Set once, when ENTER CAR first resolves this entry number against
    // the local cache (storage::findEntry) and finds nothing — see
    // CONTEXT.md: "This car number is not in the list yet. You can still
    // judge it." Kept for the life of the draft so VEHICLE DETAILS can
    // say so, even if the judge navigates away and resumes later.
    bool notInRosterYet = false;

    char participant[80] = "";
    char year[8] = "";
    char make[48] = "";
    char model[48] = "";
    char vehicleType[32] = "";
    bool makeManuallyEntered = false;
    bool modelManuallyEntered = false;

    int scoreRangeMax = 5;
    QueuedScore scores[MAX_QUEUED_SCORES];
    int scoreCount = 0;
    bool hasOverallImpression = false;
    int overallImpression = 0;

    int nominations[MAX_QUEUED_NOMINATIONS];
    int nominationCount = 0;

    bool carPhotoSaved = false;
    bool sheetPhotoSaved = false;
};

// Atomic write to /drafts/{entryNumber}.json — call after every screen
// transition within the judging flow (CONTEXT.md's resilience principle:
// a battery pull must never cost more than the last unsaved keystroke).
bool saveDraft(const DraftCar& draft);

// False if no draft exists for this entry number — the normal case for a
// car nobody has started yet, not an error.
bool loadDraft(const char* entryNumber, DraftCar* out);

bool hasDraft(const char* entryNumber);

// Called once a car is finished (moved into pending_queue) or the judge
// explicitly discards it.
bool deleteDraft(const char* entryNumber);

}  // namespace storage
