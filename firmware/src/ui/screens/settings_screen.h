// Device settings, editable on-device — the on-device half of
// requirement 1's "editable both on-device and as a plain text file on
// the card." Every field here round-trips through storage::Settings /
// /settings.txt exactly, so an on-device edit and a hand-edit of the
// text file are always interchangeable.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class SettingsScreen : public Screen {
public:
    const char* title() const override { return "Settings"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
