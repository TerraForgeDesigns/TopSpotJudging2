// PHOTOS — two required captures, Vehicle Photo and Judge Sheet Photo,
// both slots visible throughout, each with a preview once captured and
// a Retake action. Every write is verified before a slot is marked done
// — see camera::captureToFile(), which already re-opens and checks the
// file landed; a silent failure here is unrecoverable after the show
// (CONTEXT.md's resilience principle), so this screen never marks a
// slot done on faith. On any capture/write failure: "Photos cannot be
// saved. Storage is not available." — LANGUAGE.md's error standard
// applied to PROTOCOL.md's own "SD initialization failed" example.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class PhotosScreen : public Screen {
public:
    const char* title() const override { return "Photos"; }
    void build(lv_obj_t* content) override;
    void teardown() override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
