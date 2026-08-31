// The shared build-out for both the Make and Model selector screens (see
// ui/screens/make_selector_screen.* / model_selector_screen.*) — not a
// generic reusable component like searchable_selector.h, because the
// requirements are specific to vehicle entry: live filter-per-keystroke
// via an INLINE docked keyboard (not a tap-to-open overlay), a Recently
// Used section, and a data source that's already merged seed+learned
// (see storage/vehicle_db.h) rather than a flat in-memory string array.
//
// Layout, top to bottom (task's items 4-7): search box (only when
// `showSearchBox`) + an inline keyboard docked at the bottom, Recently
// Used for this show, Other / Enter Manually (always present,
// prominent), then the remaining bounded matches with a "keep typing"
// hint past VEHICLE_SELECTOR_MAX_ROWS.
#pragma once

#include <lvgl.h>

namespace ui::components {

constexpr int VEHICLE_SELECTOR_MAX_ROWS = 40;
constexpr int VEHICLE_NAME_LEN = 48;  // matches storage::vehicle_db::MAX_NAME_LEN+1

// Fills `out` (up to maxOut entries, each VEHICLE_NAME_LEN bytes) with
// bounded matches for `query` and returns the TRUE total match count
// (which may exceed maxOut) — matches storage::vehicle_db::findMakes/
// findModels' own contract directly; a thin adapter so this component
// doesn't need to know whether it's listing makes or one make's models.
using VehicleQueryFn = int (*)(void* ctx, const char* query, char out[][VEHICLE_NAME_LEN], int maxOut);

// Same shape for the Recently Used section — no query, already a short
// ordered list. Matches storage::getRecentMakes/getRecentModels.
using VehicleRecentsFn = int (*)(void* ctx, char out[][VEHICLE_NAME_LEN], int maxOut);

// Called exactly once, whichever path the judge takes:
//   - tapping a real entry from the bounded list or Recently Used:
//     manuallyEntered=false.
//   - Other/Enter Manually, after the manual-entry keyboard's Done (pre-
//     filled with whatever was in the search box — see task item 6):
//     manuallyEntered=true. Cancel on that keyboard calls nothing at all;
//     the selector screen just stays put and the judge can try again.
// Owns everything that happens next — writing the chosen name into the
// draft, recording it via storage::vehicle_recents, and navigating
// (Model selection is always a plain pop(); Make selection additionally
// runs the advance-vs-return rule — see make_selector_screen.cpp and
// DECISIONS.md's F4 entry).
using VehicleChosenCallback = void (*)(void* ctx, const char* name, bool manuallyEntered);

struct VehicleSelectorConfig {
    bool showSearchBox;              // Make: always true. Model: only when that make has >=20 models.
    const char* searchPlaceholder;   // e.g. "Search Makes" / "Search Models"
    const char* manualPlaceholder;   // the manual-entry keyboard's placeholder, e.g. "Make" / "Model"
    VehicleQueryFn query;
    void* queryCtx;
    VehicleRecentsFn recents;        // may be null — no Recently Used section shown
    void* recentsCtx;
    VehicleChosenCallback onChosen;
    void* callbackCtx;
};

void buildVehicleSelector(lv_obj_t* content, const VehicleSelectorConfig& config);

}  // namespace ui::components
