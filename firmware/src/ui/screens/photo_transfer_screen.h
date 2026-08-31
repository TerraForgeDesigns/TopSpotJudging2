// PHOTO TRANSFER — Transfer Photos (primary: safe SD removal), Send
// Photos over Wi-Fi (fallback), Clear Photos (end-of-show only). See
// storage/photo_state.h and network/photo_upload.h for the mechanics;
// this screen is purely the judge-facing surface over both, plus the
// card's photo count grouped by car.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class PhotoTransferScreen : public Screen {
public:
    const char* title() const override { return "Photos"; }
    void build(lv_obj_t* content) override;
    void teardown() override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
