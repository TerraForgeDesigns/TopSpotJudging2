#include "make_selector_screen.h"

#include <cstring>
#include <strings.h>  // strcasecmp

#include "../components/vehicle_selector_list.h"
#include "../judging_session.h"
#include "model_selector_screen.h"
#include "storage/vehicle_db.h"
#include "storage/vehicle_recents.h"

namespace ui::screens {

namespace {

int queryMakes(void* /*ctx*/, const char* query, char out[][components::VEHICLE_NAME_LEN], int maxOut) {
    storage::vehicle_db::MakeMatch matches[components::VEHICLE_SELECTOR_MAX_ROWS];
    int cap = maxOut < components::VEHICLE_SELECTOR_MAX_ROWS ? maxOut : components::VEHICLE_SELECTOR_MAX_ROWS;
    int total = storage::vehicle_db::findMakes(query, matches, cap);
    int shown = total < cap ? total : cap;
    for (int i = 0; i < shown; i++) {
        strncpy(out[i], matches[i].name, components::VEHICLE_NAME_LEN - 1);
        out[i][components::VEHICLE_NAME_LEN - 1] = '\0';
    }
    return total;
}

int recentMakes(void* /*ctx*/, char out[][components::VEHICLE_NAME_LEN], int maxOut) {
    return storage::getRecentMakes(out, maxOut);
}

// Selecting a Make: writes it, then decides whether to advance straight
// into that Make's Model selector or return to Vehicle Details — the
// task's revised navigation rule (see DECISIONS.md's F4 entry, design
// decision 5):
//   - Model was blank -> advance.
//   - Model populated but no longer valid under the NEW Make (checked via
//     vehicle_db's exact-match lookup) -> clear it, advance.
//   - Model populated and still valid (including "Make didn't actually
//     change") -> leave Model alone, return to Vehicle Details.
// "Advance" is pop() immediately followed by push(ModelSelectorScreen) —
// both synchronous, before LVGL's own render tick — so the net stack
// depth is the same either way and there's no visible flash of Vehicle
// Details in between; see screen_manager.h's "destroy immediately,
// rebuild immediately" model.
void onMakeChosen(void* /*ctx*/, const char* name, bool manuallyEntered) {
    storage::DraftCar& car = judging::current();
    bool wasBlank = car.model[0] == '\0';
    bool makeChanged = strcasecmp(car.make, name) != 0;

    strncpy(car.make, name, sizeof(car.make) - 1);
    car.make[sizeof(car.make) - 1] = '\0';
    car.makeManuallyEntered = manuallyEntered;
    storage::recordMakeUsed(name);

    bool modelCleared = false;
    if (makeChanged && car.model[0] != '\0' && !storage::vehicle_db::modelBelongsToMake(name, car.model)) {
        car.model[0] = '\0';
        car.modelManuallyEntered = false;
        modelCleared = true;
    }
    judging::save();

    screen_manager::pop();
    if (wasBlank || modelCleared) screen_manager::push(ModelSelectorScreen::create);
}

}  // namespace

void MakeSelectorScreen::build(lv_obj_t* content) {
    components::VehicleSelectorConfig config{};
    config.showSearchBox = true;
    config.searchPlaceholder = "Search Makes";
    config.manualPlaceholder = "Make";
    config.query = queryMakes;
    config.queryCtx = nullptr;
    config.recents = recentMakes;
    config.recentsCtx = nullptr;
    config.onChosen = onMakeChosen;
    config.callbackCtx = nullptr;
    components::buildVehicleSelector(content, config);
}

Screen* MakeSelectorScreen::create(void* /*arg*/) { return new MakeSelectorScreen(); }

}  // namespace ui::screens
