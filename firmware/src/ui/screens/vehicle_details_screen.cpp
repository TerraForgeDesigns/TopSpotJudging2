#include "vehicle_details_screen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../components/alert_banner.h"
#include "../components/button.h"
#include "../components/list_row.h"
#include "../components/numeric_keypad_overlay.h"
#include "../components/text_keyboard.h"
#include "../judging_session.h"
#include "judge_car_screen.h"
#include "make_selector_screen.h"
#include "model_selector_screen.h"
#include "storage/show_data.h"

namespace ui::screens {

namespace {

// 1885: the first automobile — a fixed floor, not clock-dependent. Upper
// bound is the SHOW's own known event year + 1 (one model year ahead,
// same allowance a real dealer/DMV form gives) — see
// storage::ShowInfo::eventYear and DECISIONS.md's F4 entry. This is data
// Home Base already pushed down, not a live clock reading, so no RTC/
// NTP/network is needed at judging time — only that a sync already
// happened once. Pre-first-sync (eventYear == 0), fall back to a wide
// default rather than rejecting all input.
constexpr int MIN_VEHICLE_YEAR = 1885;
constexpr int FALLBACK_MAX_VEHICLE_YEAR = 2035;

bool isValidYear(const char* text) {
    if (strlen(text) != 4) return false;
    int year = atoi(text);
    storage::ShowInfo show;
    storage::loadShowInfo(&show);
    int maxYear = show.eventYear > 0 ? show.eventYear + 1 : FALLBACK_MAX_VEHICLE_YEAR;
    return year >= MIN_VEHICLE_YEAR && year <= maxYear;
}

enum class Field { Participant, VehicleType };

void onFieldSaved(void* ctx, const char* text, bool accepted) {
    if (!accepted) return;
    Field field = static_cast<Field>(reinterpret_cast<intptr_t>(ctx));
    storage::DraftCar& car = judging::current();

    switch (field) {
        case Field::Participant: strncpy(car.participant, text, sizeof(car.participant) - 1); break;
        case Field::VehicleType: strncpy(car.vehicleType, text, sizeof(car.vehicleType) - 1); break;
    }
    judging::save();
    screen_manager::refresh();
}

void editField(Field field, const char* currentValue, const char* placeholder) {
    auto ctx = reinterpret_cast<void*>(static_cast<intptr_t>(field));
    components::textKeyboardOverlay(currentValue, placeholder, 79, onFieldSaved, ctx);
}

void onEditParticipant(void* ctx) { editField(Field::Participant, static_cast<const char*>(ctx), "Participant"); }
void onEditVehicleType(void* ctx) { editField(Field::VehicleType, static_cast<const char*>(ctx), "Vehicle Type"); }

void onYearSaved(void* /*ctx*/, const char* text, bool accepted) {
    if (!accepted) return;
    strncpy(judging::current().year, text, sizeof(judging::current().year) - 1);
    judging::save();
    screen_manager::refresh();
}

void onEditYear(void* ctx) {
    components::numericKeypadOverlay(static_cast<const char*>(ctx), "Year", /*maxLen=*/4, /*autoAcceptLen=*/4,
                                      isValidYear, "Not a plausible year for this show. Check the digits.", onYearSaved,
                                      nullptr);
}

void onEditMake(void* /*ctx*/) { screen_manager::push(MakeSelectorScreen::create); }
void onEditModel(void* /*ctx*/) { screen_manager::push(ModelSelectorScreen::create); }

void onContinue(lv_event_t*) {
    judging::current().furthestStep = storage::DraftStep::Judging;
    judging::save();
    screen_manager::push(JudgeCarScreen::create);
}

}  // namespace

void VehicleDetailsScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 8, 0);

    storage::DraftCar& car = judging::current();

    if (car.notInRosterYet) {
        components::alertBanner(content, components::AlertSeverity::Info,
                                 "This car number is not in the list yet. You can still judge it.");
    }

    components::listRow(content, "Participant", car.participant[0] != '\0' ? car.participant : "Not entered yet",
                         components::RowDot::None, onEditParticipant, car.participant);
    components::listRow(content, "Year", car.year[0] != '\0' ? car.year : "Not entered yet",
                         components::RowDot::None, onEditYear, car.year);
    components::listRow(content, "Make", car.make[0] != '\0' ? car.make : "Not entered yet",
                         components::RowDot::None, onEditMake, car.make);
    components::listRow(content, "Model", car.model[0] != '\0' ? car.model : "Not entered yet",
                         components::RowDot::None, onEditModel, car.model);
    components::listRow(content, "Vehicle Type",
                         car.vehicleType[0] != '\0' ? car.vehicleType : "Not entered yet", components::RowDot::None,
                         onEditVehicleType, car.vehicleType);

    lv_obj_t* continueBtn = components::primaryButton(content, "Continue to Scoring", 320);
    lv_obj_set_style_pad_top(continueBtn, 16, 0);
    lv_obj_add_event_cb(continueBtn, onContinue, LV_EVENT_CLICKED, nullptr);
}

Screen* VehicleDetailsScreen::create(void* /*arg*/) { return new VehicleDetailsScreen(); }

}  // namespace ui::screens
