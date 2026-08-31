// The queue of finished cars waiting to reach Home Base — see
// CONTEXT.md's resilience principle: "a judge losing an hour of work is
// the worst failure this system has." This is the single most
// crash-critical file in this firmware.
//
// Design for that: each finished car becomes its OWN file,
// /queue/{entry_number}_{closed_at_uptime_ms}.json, written exactly ONCE
// via storage::writeFileAtomic() and never modified again by this
// firmware. Because it's a brand-new filename every time (never an
// overwrite of an existing file), sd_card.h's one documented atomic-write
// gap — the narrow window between deleting an old file and renaming the
// new one in — cannot happen here at all. A battery pull at any point
// during enqueueCar() leaves EITHER no new file (the car isn't queued;
// the judge is still on the Review screen and can press Confirm again)
// OR the complete, verified file (the car is safely queued) — never a
// partial one sitting under the real filename.
//
// See firmware/TESTING.md for the physical battery-pull test procedure
// this design is built to pass.
#pragma once

#include <cstdint>

namespace storage {

constexpr int MAX_QUEUED_SCORES = 5;        // one per active Judging Category, capped at the fixed built-in set
constexpr int MAX_QUEUED_NOMINATIONS = 24;  // matches MAX_AWARDS in show_data.h

struct QueuedScore {
    int categoryId = 0;
    int points = 0;
};

// One finished car — everything PROTOCOL.md's POST /api/v1/sync needs
// for one `submissions[]` entry, cached locally until WiFi sync exists.
struct QueuedCar {
    char entryNumber[8] = "";
    char judgeName[64] = "";
    uint32_t closedAtUptimeMs = 0;  // ms since this handheld's boot — see PROTOCOL.md, no valid wall clock yet
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
    int nominations[MAX_QUEUED_NOMINATIONS];  // award ids
    int nominationCount = 0;
};

// Writes `car` as a new file under /queue/ — see this header's design
// note above for exactly what "crash-safe" means here. Returns false if
// the write could not be verified to have landed completely; the caller
// (the Review screen) must NOT treat the car as finished in that case —
// see CONTEXT.md's error standard: "Photos cannot be saved. Storage is
// not available." is the sibling of what this returns for.
bool enqueueCar(const QueuedCar& car);

// How many cars are currently queued (waiting to reach Home Base) — each
// a distinct, complete file under /queue/.
int countQueued();

// True if this entry number already has a finished, queued submission on
// this device — the REVIEW screen's "locks it on this device": ENTER CAR
// checks this and refuses to start a second attempt at an entry number
// this device already finished, rather than silently letting a judge
// re-judge the same car and create a duplicate (Home Base's own conflict
// handling would eventually catch it, but a same-device duplicate is
// avoidable before it ever needs to be).
bool isEntryQueued(const char* entryNumber);

struct QueueIntegrityReport {
    int totalFiles = 0;     // every file found under /queue/, including .tmp leftovers
    int validCars = 0;      // parsed cleanly as a complete queued car
    int orphanedTmpFiles = 0;  // a .tmp from a write that never completed — safe to delete, never "lost work"
    int unreadable = 0;     // present, not a .tmp, but failed to parse — see this function's doc below
};

// Walks every file under /queue/ and classifies it. A `.tmp` file is
// never a lost car — writeFileAtomic() only ever renames a file into its
// real name AFTER verifying it landed completely, so a `.tmp` left behind
// means the crash happened before the car was ever considered queued
// (the judge would see enqueueCar() return false and could try again).
// `unreadable` (a real filename that fails to parse) would be a genuine
// integrity problem if it ever happened — it should never happen given
// the atomic-write design, and this function exists specifically to make
// that verifiable rather than assumed; see TESTING.md. Orphaned .tmp
// files ARE cleaned up by this call (removed); everything else is
// left untouched — this function reports, it never discards a real car.
QueueIntegrityReport checkQueueIntegrity();

// Parses every valid queued car under /queue/ into `out` (caller-sized,
// up to `maxOut`). Returns the count actually written — never more than
// what's really queued, and silently skips anything checkQueueIntegrity()
// would classify as a `.tmp` or `unreadable` file (the network sync
// module calling this only wants real, complete cars to send; integrity
// diagnostics are checkQueueIntegrity()'s job, not this one's). Used by
// network::sync to build a POST /api/v1/sync request body — see
// PROTOCOL.md.
int listQueuedCars(QueuedCar* out, int maxOut);

// Removes the queued file for `entryNumber` (there is at most one, by
// construction — ENTER CAR refuses a second attempt at an entry number
// this device already finished, see isEntryQueued() above). Called ONLY
// on an explicit Home Base acknowledgement (accepted / already_recorded /
// flagged_duplicate — see PROTOCOL.md's sync response and
// network/wifi_sync.h) — never on a network failure, per CONTEXT.md's
// resilience principle. Returns true if a matching file was found and
// removed, false if none existed (not itself an error — the caller may
// be re-processing a response after a partial prior apply).
bool removeQueuedCarByEntryNumber(const char* entryNumber);

}  // namespace storage
