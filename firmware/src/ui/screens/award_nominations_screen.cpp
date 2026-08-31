#include "award_nominations_screen.h"

#include <cstdint>

#include "../components/button.h"
#include "../components/card.h"
#include "../components/checklist_row.h"
#include "../fonts/fonts.h"
#include "../judging_session.h"
#include "../theme.h"
#include "photos_screen.h"
#include "storage/show_data.h"

namespace ui::screens {

namespace {

bool isNominated(int awardId) {
    storage::DraftCar& car = judging::current();
    for (int i = 0; i < car.nominationCount; i++) {
        if (car.nominations[i] == awardId) return true;
    }
    return false;
}

void toggleNomination(void* ctx, bool checked) {
    int awardId = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    storage::DraftCar& car = judging::current();
    if (checked) {
        if (!isNominated(awardId) && car.nominationCount < storage::MAX_QUEUED_NOMINATIONS) {
            car.nominations[car.nominationCount++] = awardId;
        }
    } else {
        for (int i = 0; i < car.nominationCount; i++) {
            if (car.nominations[i] == awardId) {
                car.nominations[i] = car.nominations[car.nominationCount - 1];
                car.nominationCount--;
                break;
            }
        }
    }
    judging::save();
}

void onContinue(lv_event_t*) {
    judging::current().furthestStep = storage::DraftStep::Photos;
    judging::save();
    screen_manager::push(PhotosScreen::create);
}

}  // namespace

void AwardNominationsScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 8, 0);

    storage::ShowInfo show;
    storage::loadShowInfo(&show);

    lv_obj_t* hint = lv_label_create(content);
    lv_obj_add_style(hint, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(hint, &ui_font_plex_400_16, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_text(hint, "Tap any awards this car should be considered for. None are required.");

    if (show.awardCount == 0) {
        lv_obj_t* empty = lv_label_create(content);
        lv_obj_add_style(empty, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(empty, &ui_font_plex_400_16, 0);
        lv_label_set_text(empty, "This show has no judge-chosen awards to nominate for.");
    } else {
        lv_obj_t* list = components::card(content);
        for (int i = 0; i < show.awardCount; i++) {
            int awardId = show.awards[i].id;
            components::checklistRow(list, show.awards[i].name, isNominated(awardId), toggleNomination,
                                      reinterpret_cast<void*>(static_cast<intptr_t>(awardId)));
        }
    }

    lv_obj_t* continueBtn = components::primaryButton(content, "Continue to Photos", 320);
    lv_obj_set_style_pad_top(continueBtn, 16, 0);
    lv_obj_add_event_cb(continueBtn, onContinue, LV_EVENT_CLICKED, nullptr);
}

Screen* AwardNominationsScreen::create(void* /*arg*/) { return new AwardNominationsScreen(); }

}  // namespace ui::screens
