// Local cache of everything Home Base has told this handheld about the
// show — the pieces judging actually needs offline: active categories
// (name + order), the current score range, judge-chosen awards, whether
// Overall Impression is on, and per-entry details Home Base already
// holds. Written from a sync response (not built yet — see
// network/wifi_sync.h); read by every judging screen. All of this is
// exactly what CONTEXT.md/PROTOCOL.md call "configuration" and "cars" —
// never shown to a judge by those names, per LANGUAGE.md.
#pragma once

#include <cstdint>

namespace storage {

struct Category {
    int id = 0;
    char name[40] = "";
    int sortOrder = 0;
};

struct JudgeChosenAward {
    int id = 0;
    char name[64] = "";
};

// See CONTEXT.md's "Judging categories & scoring range" — the set is
// fixed at five, so a fixed-size array here is a real invariant, not an
// arbitrary cap.
constexpr int MAX_CATEGORIES = 5;
// CONTEXT.md's Show Awards start at 7 built-ins but the organiser can add
// more — generous headroom, not a hard show-design limit enforced here.
constexpr int MAX_AWARDS = 24;

struct ShowInfo {
    // Home Base's stable numeric id for this show (its `Show.id` primary
    // key) — unlike showName, this can never collide across a renamed or
    // duplicately-named show, so it's what storage::vehicle_recents.h
    // scopes "recently used this show" against, not the name. 0 means "no
    // show synced yet," never a real show's id.
    int showId = 0;
    char showName[128] = "";
    // ISO "YYYY-MM-DD", straight from Home Base's Show.event_date — see
    // PROTOCOL.md. Used (via eventYear below) as the Year field's upper
    // bound on the Vehicle Details screen; this is DATA Home Base already
    // has and pushes down, not a live clock reading, so it needs no RTC,
    // NTP, or network access at judging time — only that a sync already
    // happened at least once, same precondition Categories/Awards have.
    char eventDate[11] = "";
    // Parsed once from eventDate's first 4 characters at load time (see
    // show_data.cpp) — 0 if eventDate is empty/unparseable, meaning "no
    // show synced yet," the same pre-first-sync state showId=0 signals.
    int eventYear = 0;
    int scoreRangeMax = 5;
    int maxScore = 0;
    bool overallImpressionEnabled = false;
    Category categories[MAX_CATEGORIES];
    int categoryCount = 0;
    JudgeChosenAward awards[MAX_AWARDS];
    int awardCount = 0;
};

bool loadShowInfo(ShowInfo* out);
bool saveShowInfo(const ShowInfo& info);  // atomic write to /show.json

// One entry's cached details — see CONTEXT.md: "If Home Base already
// holds details for that entry number... the handheld pre-fills
// Participant, Year, Make, and Model." Every string field empty means
// "Home Base has this entry number but no details for it yet," which is
// different from the entry not existing in the cache at all (see
// findEntry() below).
struct Entry {
    char entryNumber[8] = "";
    char participant[80] = "";
    char year[8] = "";
    char make[48] = "";
    char model[48] = "";
    char vehicleType[32] = "";
};

// Looks up `entryNumber` in the local cache (/entries.json). Returns
// false if this handheld has never heard of that entry number — the
// ENTER CAR screen's "not in the list yet" case, see CONTEXT.md — never
// a hard error; the judge can still continue.
bool findEntry(const char* entryNumber, Entry* out);

// Replaces the entire local entry cache — used only when the FULL roster
// is known at once (sync_mode FULL). Use mergeEntries() for DELTA
// responses (see PROTOCOL.md).
bool saveEntries(const Entry* entries, int count);

// Overlays `delta` onto the existing cache: an entry number already
// present is replaced with the delta's version (Home Base is always the
// source of truth once it has an opinion — see PROTOCOL.md's "fill or
// correct" rule, which Home Base itself already applied before sending
// this back), one not yet present is added. This is what makes a newly
// added or corrected entry number immediately available on ENTER CAR with
// no restart — see network/wifi_sync.h and CONTEXT.md's "add cars any
// time" rule. Atomic write to /entries.json (via the same saveEntries()
// path internally).
bool mergeEntries(const Entry* delta, int deltaCount);

// How many entries are in the local cache — the diagnostics screen's
// "entry counts" (ui/screens/diagnostics_screen.h). Parses /entries.json
// the same way findEntry() does, just counting instead of matching one.
int countEntries();

}  // namespace storage
