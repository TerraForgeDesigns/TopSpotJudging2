#include "enter_car_screen.h"

#include <cstring>

#include "../components/button.h"
#include "../components/numeric_keypad.h"
#include "../fonts/fonts.h"
#include "../judging_session.h"
#include "../theme.h"
#include "award_nominations_screen.h"
#include "judge_car_screen.h"
#include "photos_screen.h"
#include "review_screen.h"
#include "../components/toast.h"
#include "storage/pending_queue.h"
#include "storage/show_data.h"
#include "vehicle_details_screen.h"

namespace ui::screens {

namespace {

constexpr int MAX_ENTRY_DIGITS = 6;

char g_typed[MAX_ENTRY_DIGITS + 1] = "";
lv_obj_t* g_display = nullptr;
lv_obj_t* g_continueBtn = nullptr;

void refreshDisplay() {
    lv_label_set_text(g_display, g_typed[0] != '\0' ? g_typed : "Type an entry number");
    components::setEnabled(g_continueBtn, g_typed[0] != '\0');
}

void onKeypad(void* /*ctx*/, char key) {
    size_t len = strlen(g_typed);
    if (key == 'C') {
        g_typed[0] = '\0';
    } else if (key == 'B') {
        if (len > 0) g_typed[len - 1] = '\0';
    } else if (len < MAX_ENTRY_DIGITS) {
        g_typed[len] = key;
        g_typed[len + 1] = '\0';
    }
    refreshDisplay();
}

// Resumes a draft (or starts a fresh one) at whichever screen its
// furthest_step points at — see storage/drafts.h and DECISIONS.md's
// resume design. A judge who left off mid-scoring lands right back in
// the middle of Judge Car, not back at the beginning.
void enterJudgingFlowAt(storage::DraftStep step) {
    switch (step) {
        case storage::DraftStep::VehicleDetails: screen_manager::push(VehicleDetailsScreen::create); break;
        case storage::DraftStep::Judging: screen_manager::push(JudgeCarScreen::create); break;
        case storage::DraftStep::Nominations: screen_manager::push(AwardNominationsScreen::create); break;
        case storage::DraftStep::Photos: screen_manager::push(PhotosScreen::create); break;
        case storage::DraftStep::Review: screen_manager::push(ReviewScreen::create); break;
    }
}

void onContinue(lv_event_t*) {
    bool resuming = storage::hasDraft(g_typed);
    if (!resuming && storage::isEntryQueued(g_typed)) {
        // "Locks it on this device" — see review_screen.h. Not resuming
        // (no draft) and already finished means this would be a second,
        // avoidable attempt at the same car on the same handheld.
        components::showToast("This car has already been judged on this device.", components::ToastSeverity::Error,
                               3500);
        return;
    }

    storage::ShowInfo show;
    storage::loadShowInfo(&show);
    judging::start(g_typed, show.scoreRangeMax);

    if (resuming) {
        enterJudgingFlowAt(judging::current().furthestStep);
        return;
    }

    storage::Entry entry;
    if (storage::findEntry(g_typed, &entry)) {
        strncpy(judging::current().participant, entry.participant, sizeof(judging::current().participant) - 1);
        strncpy(judging::current().year, entry.year, sizeof(judging::current().year) - 1);
        strncpy(judging::current().make, entry.make, sizeof(judging::current().make) - 1);
        strncpy(judging::current().model, entry.model, sizeof(judging::current().model) - 1);
        strncpy(judging::current().vehicleType, entry.vehicleType, sizeof(judging::current().vehicleType) - 1);
    } else {
        // CONTEXT.md: "This car number is not in the list yet. You can
        // still judge it." — VEHICLE DETAILS shows this, once, per the
        // flag persisted on the draft below.
        judging::current().notInRosterYet = true;
    }
    judging::save();
    screen_manager::push(VehicleDetailsScreen::create);
}

}  // namespace

void EnterCarScreen::build(lv_obj_t* content) {
    g_typed[0] = '\0';
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 16, 0);

    g_display = lv_label_create(content);
    lv_obj_add_style(g_display, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(g_display, &ui_font_archivo_800_52, 0);

    components::numericKeypad(content, onKeypad, nullptr);

    g_continueBtn = components::primaryButton(content, "Continue", 320);
    lv_obj_add_event_cb(g_continueBtn, onContinue, LV_EVENT_CLICKED, nullptr);

    refreshDisplay();
}

Screen* EnterCarScreen::create(void* /*arg*/) { return new EnterCarScreen(); }

}  // namespace ui::screens
