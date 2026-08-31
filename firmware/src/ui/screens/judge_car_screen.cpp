#include "judge_car_screen.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../components/button.h"
#include "../components/score_grid.h"
#include "../components/score_row.h"
#include "../components/toast.h"
#include "../fonts/fonts.h"
#include "../judging_session.h"
#include "../theme.h"
#include "award_nominations_screen.h"
#include "storage/show_data.h"

namespace ui::screens {

namespace {

constexpr int OVERALL_IMPRESSION_CATEGORY_ID = -1;  // never a real category id (those are Home Base row ids, always positive)

int getScore(int categoryId) {
    storage::DraftCar& car = judging::current();
    if (categoryId == OVERALL_IMPRESSION_CATEGORY_ID) {
        return car.hasOverallImpression ? car.overallImpression : -1;
    }
    for (int i = 0; i < car.scoreCount; i++) {
        if (car.scores[i].categoryId == categoryId) return car.scores[i].points;
    }
    return -1;  // unscored — never 0, see CONTEXT.md
}

void setScore(int categoryId, int points) {
    storage::DraftCar& car = judging::current();
    if (categoryId == OVERALL_IMPRESSION_CATEGORY_ID) {
        car.hasOverallImpression = true;
        car.overallImpression = points;
        judging::save();
        return;
    }
    for (int i = 0; i < car.scoreCount; i++) {
        if (car.scores[i].categoryId == categoryId) {
            car.scores[i].points = points;
            judging::save();
            return;
        }
    }
    if (car.scoreCount < storage::MAX_QUEUED_SCORES) {
        car.scores[car.scoreCount].categoryId = categoryId;
        car.scores[car.scoreCount].points = points;
        car.scoreCount++;
    }
    judging::save();
}

int runningTotal() {
    storage::DraftCar& car = judging::current();
    int total = 0;
    for (int i = 0; i < car.scoreCount; i++) total += car.scores[i].points;
    return total;  // Overall Impression deliberately excluded — CONTEXT.md: it never counts toward the total
}

void goToNominations() {
    judging::current().furthestStep = storage::DraftStep::Nominations;
    judging::save();
    screen_manager::push(AwardNominationsScreen::create);
}

// ---------------------------------------------------------------------
// Layout A — every active category on one screen, row of tap targets
// each, running total in a bottom bar. Used for the 1-5 and 1-10 ranges.
// ---------------------------------------------------------------------

void buildRowLayout(lv_obj_t* content, const storage::ShowInfo& show) {
    lv_obj_set_style_pad_all(content, 16, 0);

    lv_obj_t* scrollArea = lv_obj_create(content);
    lv_obj_remove_style_all(scrollArea);
    lv_obj_add_style(scrollArea, theme::screenBg(), 0);
    lv_obj_set_size(scrollArea, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(scrollArea, 1);
    lv_obj_set_flex_flow(scrollArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scrollArea, 14, 0);
    lv_obj_set_style_pad_bottom(scrollArea, 8, 0);

    for (int i = 0; i < show.categoryCount; i++) {
        const storage::Category& cat = show.categories[i];
        lv_obj_t* label = lv_label_create(scrollArea);
        lv_obj_add_style(label, theme::textPrimary(), 0);
        lv_obj_set_style_text_font(label, &ui_font_archivo_600_19, 0);
        lv_label_set_text(label, cat.name);

        components::scoreRow(
            scrollArea, show.scoreRangeMax, getScore(cat.id),
            [](void* ctx, int value) { setScore(reinterpret_cast<intptr_t>(ctx), value); },
            reinterpret_cast<void*>(static_cast<intptr_t>(cat.id)));
    }

    if (show.overallImpressionEnabled) {
        lv_obj_t* oiLabel = lv_label_create(scrollArea);
        lv_obj_add_style(oiLabel, theme::textPrimary(), 0);
        lv_obj_set_style_text_font(oiLabel, &ui_font_archivo_600_19, 0);
        lv_label_set_text(oiLabel, "Overall Impression");

        lv_obj_t* oiHint = lv_label_create(scrollArea);
        lv_obj_add_style(oiHint, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(oiHint, &ui_font_plex_400_14, 0);
        lv_label_set_text(oiHint, "Used only to settle ties. Does not count toward the score.");

        components::scoreRow(
            scrollArea, show.scoreRangeMax, getScore(OVERALL_IMPRESSION_CATEGORY_ID),
            [](void* ctx, int value) { setScore(reinterpret_cast<intptr_t>(ctx), value); },
            reinterpret_cast<void*>(static_cast<intptr_t>(OVERALL_IMPRESSION_CATEGORY_ID)));
    }

    // Bottom bar — 56px, not a right rail (see this screen's header
    // comment for why): fixed height, NOT flex_grow, so it always sits
    // pinned at the bottom of `content` while scrollArea above absorbs
    // whatever height is left.
    lv_obj_t* bar = lv_obj_create(content);
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, theme::card(), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_size(bar, LV_PCT(100), 64);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* totalLabel = lv_label_create(bar);
    lv_obj_add_style(totalLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(totalLabel, &ui_font_archivo_700_30, 0);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", runningTotal(), show.maxScore);
    lv_label_set_text(totalLabel, buf);

    lv_obj_t* continueBtn = components::primaryButton(bar, "Continue", LV_SIZE_CONTENT);
    lv_obj_set_height(continueBtn, 48);
    lv_obj_add_event_cb(
        continueBtn,
        [](lv_event_t*) {
            storage::ShowInfo show;
            storage::loadShowInfo(&show);
            for (int i = 0; i < show.categoryCount; i++) {
                if (getScore(show.categories[i].id) < 0) {
                    char msg[96];
                    snprintf(msg, sizeof(msg), "%s has not been scored. Choose a %s score before continuing.",
                              show.categories[i].name, show.categories[i].name);
                    components::showToast(msg, components::ToastSeverity::Error, 3500);
                    return;
                }
            }
            if (show.overallImpressionEnabled && getScore(OVERALL_IMPRESSION_CATEGORY_ID) < 0) {
                components::showToast("Give an Overall Impression score before continuing.",
                                       components::ToastSeverity::Error, 3500);
                return;
            }
            goToNominations();
        },
        LV_EVENT_CLICKED, nullptr);
}

// ---------------------------------------------------------------------
// Layout B — one category per screen, 5x5 grid. Used for the 1-25 range.
// ---------------------------------------------------------------------

// Which step (0..categoryCount-1 = a real category, categoryCount =
// Overall Impression if enabled) this screen is currently showing.
// Reset to the count of already-scored categories only on a genuinely
// fresh entry into this screen (detected by entry-number change) — an
// internal Next/Back stays on the same DraftCar/session and must not
// reset step position on every screen_manager::refresh(). Simplification
// worth flagging: this resumes to "the first category still unscored,
// scanning in order" rather than tracking an exact last-viewed step — if
// a judge went back and re-visited an earlier category out of order
// before a crash, resume lands at the first GAP, not necessarily where
// they physically were. Never loses a score either way; see DECISIONS.md.
int g_step = 0;
char g_stepSessionEntry[8] = "";

void resetStepIfNewSession() {
    if (strcmp(g_stepSessionEntry, judging::entryNumber()) != 0) {
        strncpy(g_stepSessionEntry, judging::entryNumber(), sizeof(g_stepSessionEntry) - 1);
        storage::ShowInfo show;
        storage::loadShowInfo(&show);
        g_step = 0;
        while (g_step < show.categoryCount && getScore(show.categories[g_step].id) >= 0) g_step++;
    }
}

void buildGridLayout(lv_obj_t* content, const storage::ShowInfo& show) {
    resetStepIfNewSession();
    int totalSteps = show.categoryCount + (show.overallImpressionEnabled ? 1 : 0);
    if (g_step >= totalSteps) g_step = totalSteps - 1;
    bool onOverallImpression = g_step == show.categoryCount;
    int categoryId = onOverallImpression ? OVERALL_IMPRESSION_CATEGORY_ID : show.categories[g_step].id;
    const char* stepLabel = onOverallImpression ? "Overall Impression" : show.categories[g_step].name;

    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 10, 0);

    char progress[32];
    snprintf(progress, sizeof(progress), onOverallImpression ? "Overall Impression" : "Category %d of %d", g_step + 1,
              show.categoryCount);
    lv_obj_t* progressLabel = lv_label_create(content);
    lv_obj_add_style(progressLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(progressLabel, &ui_font_plex_500_19, 0);
    lv_label_set_text(progressLabel, progress);

    lv_obj_t* nameLabel = lv_label_create(content);
    lv_obj_add_style(nameLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(nameLabel, &ui_font_archivo_700_30, 0);
    lv_label_set_text(nameLabel, stepLabel);

    if (onOverallImpression) {
        lv_obj_t* hint = lv_label_create(content);
        lv_obj_add_style(hint, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(hint, &ui_font_plex_400_14, 0);
        lv_label_set_text(hint, "Used only to settle ties. Does not count toward the score.");
    }

    components::scoreGrid(
        content, getScore(categoryId), [](void* ctx, int value) { setScore(reinterpret_cast<intptr_t>(ctx), value); },
        reinterpret_cast<void*>(static_cast<intptr_t>(categoryId)));

    char totalBuf[32];
    snprintf(totalBuf, sizeof(totalBuf), "Total %d / %d", runningTotal(), show.maxScore);
    lv_obj_t* totalLabel = lv_label_create(content);
    lv_obj_add_style(totalLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(totalLabel, &ui_font_plex_400_16, 0);
    lv_label_set_text(totalLabel, totalBuf);

    lv_obj_t* navRow = lv_obj_create(content);
    lv_obj_remove_style_all(navRow);
    lv_obj_set_size(navRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(navRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(navRow, 10, 0);
    lv_obj_clear_flag(navRow, LV_OBJ_FLAG_SCROLLABLE);

    if (g_step > 0) {
        lv_obj_t* backBtn = components::secondaryButton(navRow, "Previous", 150);
        lv_obj_add_event_cb(
            backBtn, [](lv_event_t*) { g_step--; screen_manager::refresh(); }, LV_EVENT_CLICKED, nullptr);
    }

    bool isLastStep = g_step == totalSteps - 1;
    lv_obj_t* nextBtn = components::primaryButton(navRow, isLastStep ? "Continue" : "Next", 150);
    lv_obj_add_event_cb(
        nextBtn,
        [](lv_event_t*) {
            storage::ShowInfo show;
            storage::loadShowInfo(&show);
            bool onOI = g_step == show.categoryCount;
            int catId = onOI ? OVERALL_IMPRESSION_CATEGORY_ID : show.categories[g_step].id;
            const char* name = onOI ? "Overall Impression" : show.categories[g_step].name;
            if (getScore(catId) < 0) {
                char msg[96];
                snprintf(msg, sizeof(msg), "%s has not been scored. Choose a %s score before continuing.", name,
                          name);
                components::showToast(msg, components::ToastSeverity::Error, 3500);
                return;
            }
            int total = show.categoryCount + (show.overallImpressionEnabled ? 1 : 0);
            if (g_step + 1 >= total) {
                goToNominations();
            } else {
                g_step++;
                screen_manager::refresh();
            }
        },
        LV_EVENT_CLICKED, nullptr);
}

}  // namespace

void JudgeCarScreen::build(lv_obj_t* content) {
    storage::ShowInfo show;
    storage::loadShowInfo(&show);

    // Adopt the show's CURRENT range only while this draft is still
    // genuinely untouched (no scores entered yet) — matches
    // judging_session.h's own documented rule ("only applied when
    // starting fresh, since a resumed draft already has whatever range
    // was in effect when it was started"). Before F5 (network sync) this
    // couldn't matter — the range never changed at runtime. Now it can:
    // a judge could be mid-way through scoring a car (not yet queued)
    // when a background sync escalates the show's range. Blindly
    // overwriting scoreRangeMax here would silently reinterpret
    // already-entered raw points on a new scale without running them
    // through the actual conversion formula — see CONTEXT.md's score
    // conversion section, which describes converting a FINISHED
    // submission, not silently relabeling an in-progress one.
    storage::DraftCar& car = judging::current();
    if (car.scoreCount == 0 && !car.hasOverallImpression) {
        car.scoreRangeMax = show.scoreRangeMax;
    }

    if (show.scoreRangeMax <= 10) {
        buildRowLayout(content, show);
    } else {
        buildGridLayout(content, show);
    }
}

Screen* JudgeCarScreen::create(void* /*arg*/) { return new JudgeCarScreen(); }

}  // namespace ui::screens
