// SCORING UPDATED — the task's exact full-screen notice for a score-range
// escalation (CONTEXT.md: the range only ever moves up, never down, and
// already-sent scores are converted automatically, never requiring the
// judge to revisit a car). Pushed on top of whatever screen the judge was
// already on by network::sync when a sync response's configuration
// reports a different scoreRangeMax than what was cached before — see
// network/wifi_sync.h. Never navigates the judge away from an
// in-progress car; Continue just pops back to exactly where they were.
//
// LANGUAGE.md: never the words range, scale, convert, revision, or
// recalculate on this screen — checked at review time, not just by
// convention.
#pragma once

#include "../screen_manager.h"

namespace ui::screens {

class ScoringUpdatedScreen : public Screen {
public:
    const char* title() const override { return "Scoring Updated"; }
    void build(lv_obj_t* content) override;

    // `arg` is the show's NEW scoreRangeMax, passed as
    // reinterpret_cast<void*>(static_cast<intptr_t>(newRangeMax)) —
    // matches this project's established plain-C-callback context
    // convention (see settings_screen.cpp's Field enum for the same
    // pattern).
    static Screen* create(void* arg);
};

}  // namespace ui::screens
