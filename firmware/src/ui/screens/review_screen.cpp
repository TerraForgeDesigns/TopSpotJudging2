#include "review_screen.h"

#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

#include "../components/button.h"
#include "../components/card.h"
#include "../components/toast.h"
#include "../fonts/fonts.h"
#include "../judging_session.h"
#include "../theme.h"
#include "storage/sd_card.h"
#include "storage/show_data.h"

namespace ui::screens {

namespace {

constexpr int PREVIEW_BOX_W = 160;
constexpr int ASSUMED_CAPTURE_W = 1280;
constexpr int ASSUMED_CAPTURE_H = 720;
constexpr size_t MAX_PREVIEW_JPEG_SIZE = 512 * 1024;

uint8_t* g_carPreviewBuf = nullptr;
uint8_t* g_sheetPreviewBuf = nullptr;
lv_img_dsc_t g_carPreviewDsc;
lv_img_dsc_t g_sheetPreviewDsc;

void freePreview(uint8_t** buf) {
    if (*buf != nullptr) {
        heap_caps_free(*buf);
        *buf = nullptr;
    }
}

void addThumbnail(lv_obj_t* parent, const char* suffix, uint8_t** buf, lv_img_dsc_t* dsc) {
    char path[32];
    snprintf(path, sizeof(path), "/%s_%s.jpg", judging::entryNumber(), suffix);

    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, theme::raisedSurface(), 0);
    lv_obj_set_size(box, PREVIEW_BOX_W, PREVIEW_BOX_W * ASSUMED_CAPTURE_H / ASSUMED_CAPTURE_W);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    *buf = static_cast<uint8_t*>(heap_caps_malloc(MAX_PREVIEW_JPEG_SIZE, MALLOC_CAP_SPIRAM));
    int n = *buf != nullptr ? storage::readFile(path, *buf, MAX_PREVIEW_JPEG_SIZE) : -1;
    if (n > 0) {
        memset(dsc, 0, sizeof(*dsc));
        dsc->header.cf = LV_IMG_CF_RAW;
        dsc->header.w = ASSUMED_CAPTURE_W;
        dsc->header.h = ASSUMED_CAPTURE_H;
        dsc->data_size = static_cast<uint32_t>(n);
        dsc->data = *buf;
        lv_obj_t* img = lv_img_create(box);
        lv_img_set_src(img, dsc);
        lv_img_set_zoom(img, static_cast<uint16_t>(256 * PREVIEW_BOX_W / ASSUMED_CAPTURE_W));
        lv_obj_center(img);
    } else {
        freePreview(buf);
        lv_obj_t* label = lv_label_create(box);
        lv_obj_add_style(label, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(label, &ui_font_plex_400_14, 0);
        lv_label_set_text(label, "Not available");
        lv_obj_center(label);
    }
}

void addRow(lv_obj_t* parent, const char* label, const char* value) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* l = lv_label_create(row);
    lv_obj_add_style(l, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(l, &ui_font_plex_400_16, 0);
    lv_label_set_text(l, label);

    lv_obj_t* v = lv_label_create(row);
    lv_obj_add_style(v, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(v, &ui_font_plex_400_16, 0);
    lv_label_set_text(v, value[0] != '\0' ? value : "Not entered");
}

void onConfirm(lv_event_t*) {
    char entryNumber[8];
    strncpy(entryNumber, judging::entryNumber(), sizeof(entryNumber) - 1);
    entryNumber[sizeof(entryNumber) - 1] = '\0';

    if (judging::finish()) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "Car %s is locked in and queued to send to Home Base next time this device connects.", entryNumber);
        components::showToast(msg, components::ToastSeverity::Success, 4000);
        screen_manager::popToRoot();
    } else {
        components::showToast("This car could not be saved. Check the memory card and try again.",
                               components::ToastSeverity::Error, 4000);
    }
}

}  // namespace

void ReviewScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 10, 0);

    storage::DraftCar& car = judging::current();
    storage::ShowInfo show;
    storage::loadShowInfo(&show);

    lv_obj_t* detailsCard = components::card(content);
    lv_obj_t* header = lv_label_create(detailsCard);
    lv_obj_add_style(header, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(header, &ui_font_archivo_700_23, 0);
    lv_label_set_text(header, car.entryNumber);
    addRow(detailsCard, "Participant", car.participant);
    addRow(detailsCard, "Year", car.year);
    addRow(detailsCard, "Make", car.make);
    addRow(detailsCard, "Model", car.model);
    addRow(detailsCard, "Vehicle Type", car.vehicleType);

    lv_obj_t* scoresCard = components::card(content);
    lv_obj_t* scoresHeader = lv_label_create(scoresCard);
    lv_obj_add_style(scoresHeader, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(scoresHeader, &ui_font_plex_600_23, 0);
    lv_label_set_text(scoresHeader, "Scores");

    int total = 0;
    for (int i = 0; i < show.categoryCount; i++) {
        int points = -1;
        for (int j = 0; j < car.scoreCount; j++) {
            if (car.scores[j].categoryId == show.categories[i].id) {
                points = car.scores[j].points;
                break;
            }
        }
        char valueBuf[8];
        snprintf(valueBuf, sizeof(valueBuf), points >= 0 ? "%d" : "-", points);
        total += points >= 0 ? points : 0;
        addRow(scoresCard, show.categories[i].name, valueBuf);
    }
    if (show.overallImpressionEnabled) {
        char oiBuf[8];
        if (car.hasOverallImpression) {
            snprintf(oiBuf, sizeof(oiBuf), "%d", car.overallImpression);
        } else {
            snprintf(oiBuf, sizeof(oiBuf), "-");
        }
        addRow(scoresCard, "Overall Impression", oiBuf);
    }
    char totalBuf[32];
    snprintf(totalBuf, sizeof(totalBuf), "%d / %d", total, show.maxScore);
    addRow(scoresCard, "Total", totalBuf);

    lv_obj_t* photosCard = components::card(content);
    lv_obj_t* photosHeader = lv_label_create(photosCard);
    lv_obj_add_style(photosHeader, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(photosHeader, &ui_font_plex_600_23, 0);
    lv_label_set_text(photosHeader, "Photos");
    lv_obj_t* photoRow = lv_obj_create(photosCard);
    lv_obj_remove_style_all(photoRow);
    lv_obj_set_size(photoRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(photoRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(photoRow, 12, 0);
    lv_obj_clear_flag(photoRow, LV_OBJ_FLAG_SCROLLABLE);
    addThumbnail(photoRow, "car", &g_carPreviewBuf, &g_carPreviewDsc);
    addThumbnail(photoRow, "sheet", &g_sheetPreviewBuf, &g_sheetPreviewDsc);

    lv_obj_t* nomCard = components::card(content);
    lv_obj_t* nomHeader = lv_label_create(nomCard);
    lv_obj_add_style(nomHeader, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(nomHeader, &ui_font_plex_600_23, 0);
    lv_label_set_text(nomHeader, "Award Nominations");
    if (car.nominationCount == 0) {
        lv_obj_t* none = lv_label_create(nomCard);
        lv_obj_add_style(none, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(none, &ui_font_plex_400_16, 0);
        lv_label_set_text(none, "None");
    } else {
        for (int i = 0; i < car.nominationCount; i++) {
            for (int j = 0; j < show.awardCount; j++) {
                if (show.awards[j].id == car.nominations[i]) {
                    lv_obj_t* item = lv_label_create(nomCard);
                    lv_obj_add_style(item, theme::textPrimary(), 0);
                    lv_obj_set_style_text_font(item, &ui_font_plex_400_16, 0);
                    lv_label_set_text(item, show.awards[j].name);
                    break;
                }
            }
        }
    }

    if (!car.carPhotoSaved || !car.sheetPhotoSaved) {
        lv_obj_t* warn = lv_label_create(content);
        lv_obj_add_style(warn, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(warn, &ui_font_plex_400_16, 0);
        lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(warn, LV_PCT(100));
        lv_label_set_text(warn, "Both photos are required before this car can be confirmed. Go back to Photos.");
    }

    lv_obj_t* confirmBtn = components::primaryButton(content, "Confirm", 320);
    lv_obj_set_style_pad_top(confirmBtn, 8, 0);
    components::setEnabled(confirmBtn, car.carPhotoSaved && car.sheetPhotoSaved);
    lv_obj_add_event_cb(confirmBtn, onConfirm, LV_EVENT_CLICKED, nullptr);
}

void ReviewScreen::teardown() {
    freePreview(&g_carPreviewBuf);
    freePreview(&g_sheetPreviewBuf);
}

Screen* ReviewScreen::create(void* /*arg*/) { return new ReviewScreen(); }

}  // namespace ui::screens
