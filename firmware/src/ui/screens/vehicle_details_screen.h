// VEHICLE DETAILS — Participant, Year, Make, Model, Vehicle Type as
// tappable fields, pre-filled with whatever Home Base already sent for
// this entry number. Tapping a field opens the plain text/numeric
// keyboard overlay from ui/components/text_keyboard.h for now — full
// FW4 behavior (e.g. a searchable make/model selector) is a later
// prompt's job; this screen's job is to wire the fields and pre-fill.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class VehicleDetailsScreen : public Screen {
public:
    const char* title() const override { return "Vehicle Details"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
