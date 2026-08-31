// REVIEW — all scores, total, both photo thumbnails, car details, and
// nominations, in one place before the car is locked in. Confirm
// finishes the car (moves it into the queue — see judging_session.h),
// locks it on this device (see teardown/onShow: a finished car is never
// re-enterable through ENTER CAR — its draft is gone, and re-typing the
// same entry number starts a brand-new, empty attempt), and says what
// that means. A car whose photos failed to save can never reach this
// confirm action successfully — see requirement 4.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class ReviewScreen : public Screen {
public:
    const char* title() const override { return "Review"; }
    void build(lv_obj_t* content) override;
    void teardown() override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
