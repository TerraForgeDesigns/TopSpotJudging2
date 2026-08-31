// VEHICLE DETAILS — Participant, Year, Make, Model, Vehicle Type as
// tappable fields, pre-filled with whatever Home Base already sent for
// this entry number. Participant/Vehicle Type open the plain text
// keyboard overlay (ui/components/text_keyboard.h); Year opens the
// no-punctuation numeric keypad overlay
// (ui/components/numeric_keypad_overlay.h), auto-closing on a plausible
// 4-digit year; Make/Model push the flash-database-backed selector
// screens (ui/screens/make_selector_screen.h,
// ui/screens/model_selector_screen.h) — see DECISIONS.md's F4 entry.
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
