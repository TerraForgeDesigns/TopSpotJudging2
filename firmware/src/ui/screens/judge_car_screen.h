// JUDGE CAR — two layouts chosen at runtime from the show's current
// score range (never hardcoded, see storage/show_data.h::ShowInfo,
// read fresh on every build()):
//
//   1-5 / 1-10: every active category on one screen, a row of tap
//   targets each (components/score_row.h), running total in a bottom
//   bar — a right rail would leave 1-10's ten targets at 38px, under
//   the 44px minimum; a bottom bar gives 56px.
//
//   1-25: one category per screen, a 5x5 grid (components/score_grid.h)
//   at ~144x58px targets, with a progress indicator through the active
//   categories — 25 targets in a single row would be ~21px wide.
//
// Unscored is a dash, never 0 (see CONTEXT.md's scoring section) — a
// category only has a score once the judge taps a target for it.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class JudgeCarScreen : public Screen {
public:
    const char* title() const override { return "Judge Car"; }
    void build(lv_obj_t* content) override;

    static Screen* create(void* arg);
};

}  // namespace ui::screens
