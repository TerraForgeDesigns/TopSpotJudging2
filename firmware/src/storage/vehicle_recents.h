// "Recently used this show" — the Make/Model selector's shortcut list
// (see ui/components/vehicle_selector_list.*): at a show with forty
// Chevrolets, this makes the fortieth a single tap, saving more judge
// time than the search box does. See CONTEXT.md's vehicle-entry section.
//
// SD-backed (reuses storage::writeFileAtomic — same crash-safety tier as
// drafts), but this is a convenience layer, NOT the core vehicle lookup:
// unlike storage::vehicle_db, it's allowed to come back empty with no SD
// card present — recents are lost, not the ability to pick a vehicle.
//
// Scoped by Show ID (storage::ShowInfo::showId), not show name — a name
// can be duplicated or edited without the show actually changing, and a
// renamed show must NOT look like a brand-new one to a judge who's been
// working it all day. A stored file whose show id doesn't match the
// current one is treated as stale and reset, same principle as drafts
// resuming only within their own show's lifetime.
#pragma once

namespace storage {

// Records `make` as most-recently-used for the CURRENT show (per
// ShowInfo::showId). Safe to call even with no SD card or no show cached
// yet — a no-op in either case, never a blocking failure.
void recordMakeUsed(const char* make);

// Records `model` as most-recently-used for `make`, for the current show.
void recordModelUsed(const char* make, const char* model);

// Writes up to `maxOut` recently-used make names (most recent first) into
// `out` (each MAX_NAME_LEN+1 bytes — see vehicle_db.h). Returns the
// count actually written. Empty (0) if no SD card, no show cached, or
// nothing recorded yet for this show — never an error case to the caller.
int getRecentMakes(char out[][48], int maxOut);

// Same idea, scoped to `make`'s recently-used models.
int getRecentModels(const char* make, char out[][48], int maxOut);

}  // namespace storage
