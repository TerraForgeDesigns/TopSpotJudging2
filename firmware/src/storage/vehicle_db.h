// The vehicle make/model database — flash-mapped, searched in place, no
// SD card required. Two layers, merged transparently at query time (the
// judge never sees which one a name came from):
//
//   SEED — a read-only binary in its own flash partition (`vehicle_seed`,
//   see ../../partitions.csv), built offline by tools/build_vehicle_seed.py
//   from tools/vehicle_seed_source.json (NHTSA vPIC data plus a curated
//   classic/motorcycle supplement — see DECISIONS.md's F4 entry for the
//   full provenance). Memory-mapped via esp_partition_mmap() and searched
//   directly against mapped flash, never parsed into a heap/PSRAM object
//   tree at startup.
//
//   LEARNED — a small Preferences (NVS) store for names Home Base later
//   approves and pushes down as PROTOCOL.md's `vehicle_additions`. That
//   sync path doesn't exist yet (network/wifi_sync.h isn't built) — this
//   module only wires the read (merge-at-query) and write API; the store
//   sits empty on a real device until sync lands and calls
//   saveLearnedVehicle(). Not a stub pretending to be more than it is —
//   see DECISIONS.md.
//
// Missing/never-flashed SEED partition degrades to zero seed matches,
// never a crash — Other/Enter Manually must always work regardless of
// whether `pio run -t upload-vehicle-seed` has ever been run on this
// board (see partitions.csv and platformio.ini's custom target).
#pragma once

#include <cstdint>

namespace storage::vehicle_db {

constexpr int MAX_NAME_LEN = 47;  // matches DraftCar.make/model — char[48]

struct MakeMatch {
    char name[MAX_NAME_LEN + 1];
};

struct ModelMatch {
    char name[MAX_NAME_LEN + 1];
};

// Maps the vehicle_seed partition read-only. Safe to call even if the
// partition was never flashed or fails validation — every function below
// then simply reports zero seed matches rather than crashing. Idempotent;
// call once at boot alongside the other storage::*::begin()-style init.
void init();

// True once init() has successfully mapped a valid seed partition — for
// diagnostics/DECISIONS.md verification only, never gates the UI (Other
// must work either way).
bool seedAvailable();

// Bounded, case-insensitive SUBSTRING match (matches anywhere in the
// name, not just a prefix — "vette" finds "Corvette") across every make
// name, seed + learned merged and de-duplicated case-insensitively.
// Writes up to `maxOut` matches into `out` (sorted), returns the TRUE
// total match count, which may exceed `maxOut` — the caller uses that to
// show "N more — keep typing to narrow it down." Empty `query` matches
// everything (the initial, unfiltered list). A full linear scan, not a
// binary-search-narrowed one: with only a few hundred makes, that's well
// under a millisecond on this MCU, and — unlike binary search — it's the
// only way to honestly support "anywhere in the name," not just a
// prefix. See findExactMake()'s doc below for where binary search
// actually earns its keep in this module.
int findMakes(const char* query, MakeMatch* out, int maxOut);

// Same idea, scoped to one make's models (seed + learned). `make` is
// matched case-insensitively and exactly (see findExactMake) — an
// unknown make returns 0 with no matches, not an error.
int findModels(const char* make, const char* query, ModelMatch* out, int maxOut);

// Exact (case-insensitive) membership check — is `model` one of `make`'s
// known models (seed or learned)? Drives Vehicle Details' rule: a Make
// change clears Model unless the existing Model is still valid under the
// newly-chosen Make — see ui/screens/make_selector_screen.cpp and
// DECISIONS.md's F4 entry.
bool modelBelongsToMake(const char* make, const char* model);

// --- Learned store (Preferences-backed "veh_learn" namespace) ---

// Write path for a FUTURE sync module — see this header's top comment.
// Adds (make, model) as a learned pair; a make with no prior learned
// entry is created, an existing one gets the model appended
// (deduplicated against both its own learned models and the seed).
void saveLearnedVehicle(const char* make, const char* model);

}  // namespace storage::vehicle_db
