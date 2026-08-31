#include "model_selector_screen.h"

#include <cstring>

#include "../components/vehicle_selector_list.h"
#include "../judging_session.h"
#include "storage/vehicle_db.h"
#include "storage/vehicle_recents.h"

namespace ui::screens {

namespace {

constexpr int SEARCH_BOX_THRESHOLD = 20;  // task item 5: "fewer than about 20 models: show them with no search box"

int queryModels(void* /*ctx*/, const char* query, char out[][components::VEHICLE_NAME_LEN], int maxOut) {
    storage::vehicle_db::ModelMatch matches[components::VEHICLE_SELECTOR_MAX_ROWS];
    int cap = maxOut < components::VEHICLE_SELECTOR_MAX_ROWS ? maxOut : components::VEHICLE_SELECTOR_MAX_ROWS;
    int total = storage::vehicle_db::findModels(judging::current().make, query, matches, cap);
    int shown = total < cap ? total : cap;
    for (int i = 0; i < shown; i++) {
        strncpy(out[i], matches[i].name, components::VEHICLE_NAME_LEN - 1);
        out[i][components::VEHICLE_NAME_LEN - 1] = '\0';
    }
    return total;
}

int recentModels(void* /*ctx*/, char out[][components::VEHICLE_NAME_LEN], int maxOut) {
    return storage::getRecentModels(judging::current().make, out, maxOut);
}

// Model selection is always a plain pop() back to Vehicle Details — see
// make_selector_screen.cpp's onMakeChosen for the one place navigation
// here is more than that (auto-advancing INTO this screen).
void onModelChosen(void* /*ctx*/, const char* name, bool manuallyEntered) {
    storage::DraftCar& car = judging::current();
    strncpy(car.model, name, sizeof(car.model) - 1);
    car.model[sizeof(car.model) - 1] = '\0';
    car.modelManuallyEntered = manuallyEntered;
    storage::recordModelUsed(car.make, name);
    judging::save();
    screen_manager::pop();
}

}  // namespace

void ModelSelectorScreen::build(lv_obj_t* content) {
    storage::vehicle_db::ModelMatch probe[1];
    int totalModels = storage::vehicle_db::findModels(judging::current().make, "", probe, 1);

    components::VehicleSelectorConfig config{};
    config.showSearchBox = totalModels >= SEARCH_BOX_THRESHOLD;
    config.searchPlaceholder = "Search Models";
    config.manualPlaceholder = "Model";
    config.query = queryModels;
    config.queryCtx = nullptr;
    config.recents = recentModels;
    config.recentsCtx = nullptr;
    config.onChosen = onModelChosen;
    config.callbackCtx = nullptr;
    components::buildVehicleSelector(content, config);
}

Screen* ModelSelectorScreen::create(void* /*arg*/) { return new ModelSelectorScreen(); }

}  // namespace ui::screens
