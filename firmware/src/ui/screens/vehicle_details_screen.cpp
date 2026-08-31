#include "vehicle_details_screen.h"

#include <cstring>

#include "../components/alert_banner.h"
#include "../components/button.h"
#include "../components/list_row.h"
#include "../components/text_keyboard.h"
#include "../judging_session.h"
#include "judge_car_screen.h"

namespace ui::screens {

namespace {

enum class Field { Participant, Year, Make, Model, VehicleType };

void onFieldSaved(void* ctx, const char* text, bool accepted) {
    if (!accepted) return;
    Field field = static_cast<Field>(reinterpret_cast<intptr_t>(ctx));
    storage::DraftCar& car = judging::current();

    switch (field) {
        case Field::Participant: strncpy(car.participant, text, sizeof(car.participant) - 1); break;
        case Field::Year: strncpy(car.year, text, sizeof(car.year) - 1); break;
        case Field::Make:
            strncpy(car.make, text, sizeof(car.make) - 1);
            car.makeManuallyEntered = true;  // free text, not picked from an approved list — see PROTOCOL.md
            break;
        case Field::Model:
            strncpy(car.model, text, sizeof(car.model) - 1);
            car.modelManuallyEntered = true;
            break;
        case Field::VehicleType: strncpy(car.vehicleType, text, sizeof(car.vehicleType) - 1); break;
    }
    judging::save();
    screen_manager::refresh();
}

void editField(Field field, const char* currentValue, const char* placeholder, bool numeric) {
    auto ctx = reinterpret_cast<void*>(static_cast<intptr_t>(field));
    if (numeric) {
        components::numericKeyboardOverlay(currentValue, placeholder, 8, onFieldSaved, ctx);
    } else {
        components::textKeyboardOverlay(currentValue, placeholder, 79, onFieldSaved, ctx);
    }
}

void onEditParticipant(void* ctx) { editField(Field::Participant, static_cast<const char*>(ctx), "Participant", false); }
void onEditYear(void* ctx) { editField(Field::Year, static_cast<const char*>(ctx), "Year", true); }
void onEditMake(void* ctx) { editField(Field::Make, static_cast<const char*>(ctx), "Make", false); }
void onEditModel(void* ctx) { editField(Field::Model, static_cast<const char*>(ctx), "Model", false); }
void onEditVehicleType(void* ctx) { editField(Field::VehicleType, static_cast<const char*>(ctx), "Vehicle Type", false); }

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
