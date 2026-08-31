#include "photos_screen.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

#include "../components/alert_banner.h"
#include "../components/button.h"
#include "../components/toast.h"
#include "../fonts/fonts.h"
#include "../judging_session.h"
#include "../theme.h"
#include "camera/camera.h"
#include "review_screen.h"
#include "storage/sd_card.h"

namespace ui::screens {

namespace {

// CAM_IMAGE_MODE_HD (see camera::CAPTURE_MODE_PHOTO) — used only to size
// the preview's zoom-to-fit; if that capture mode ever changes, this
// must change with it. Not load-bearing for correctness (a wrong guess
// here just makes the preview a slightly wrong size, never a data
// problem), so it's a soft assumption, not a hardware fact this
// firmware relies on for anything crash-safety-relevant.
constexpr int ASSUMED_CAPTURE_W = 1280;
constexpr int ASSUMED_CAPTURE_H = 720;
constexpr int PREVIEW_BOX_W = 280;

constexpr size_t MAX_PREVIEW_JPEG_SIZE = 512 * 1024;  // generous for an HD JPEG; a bigger file just won't preview

// Two independent preview buffers (PSRAM) + LVGL image descriptors — one
// per slot. Freed in PhotosScreen::teardown(), which screen_manager
// guarantees is called before this screen's widget tree (including the
// lv_img objects pointing at these buffers) is deleted — never a
// dangling image source.
uint8_t* g_carPreviewBuf = nullptr;
uint8_t* g_sheetPreviewBuf = nullptr;
lv_img_dsc_t g_carPreviewDsc;
lv_img_dsc_t g_sheetPreviewDsc;

lv_obj_t* g_carSlot = nullptr;
lv_obj_t* g_sheetSlot = nullptr;

void buildPath(const char* suffix, char* out, size_t outSize) {
    snprintf(out, outSize, "/%s_%s.jpg", judging::entryNumber(), suffix);
}

void freePreview(uint8_t** buf) {
    if (*buf != nullptr) {
        heap_caps_free(*buf);
        *buf = nullptr;
    }
}

// Loads `path` into a fresh PSRAM buffer and points `dsc` at it as a raw
// (still-compressed) image source — LVGL's SJPG decoder recognizes the
// JPEG header itself; no LVGL filesystem driver needed (see lv_conf.h's
// note on why LV_USE_SJPG is on but LV_USE_FS_* stays off).
bool loadPreview(const char* path, uint8_t** bufOut, lv_img_dsc_t* dsc) {
    freePreview(bufOut);
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(MAX_PREVIEW_JPEG_SIZE, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) return false;
    int n = storage::readFile(path, buf, MAX_PREVIEW_JPEG_SIZE);
    if (n <= 0) {
        heap_caps_free(buf);
        return false;
    }
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.cf = LV_IMG_CF_RAW;
    dsc->header.w = ASSUMED_CAPTURE_W;
    dsc->header.h = ASSUMED_CAPTURE_H;
    dsc->data_size = static_cast<uint32_t>(n);
    dsc->data = buf;
    *bufOut = buf;
    return true;
}

void refreshSlot(lv_obj_t* slotContainer, bool saved, uint8_t** buf, lv_img_dsc_t* dsc, const char* path) {
    lv_obj_clean(slotContainer);

    if (saved && loadPreview(path, buf, dsc)) {
        lv_obj_t* img = lv_img_create(slotContainer);
        lv_img_set_src(img, dsc);
        uint16_t zoom = static_cast<uint16_t>(256 * PREVIEW_BOX_W / ASSUMED_CAPTURE_W);
        lv_img_set_zoom(img, zoom);
        lv_obj_center(img);
    } else {
        lv_obj_t* placeholder = lv_label_create(slotContainer);
        lv_obj_add_style(placeholder, theme::textSecondary(), 0);
        lv_obj_set_style_text_font(placeholder, &ui_font_plex_400_16, 0);
        lv_label_set_text(placeholder, "No photo yet");
        lv_obj_center(placeholder);
    }
}

void capture(const char* suffix, bool* savedFlag, lv_obj_t* slotContainer, uint8_t** buf, lv_img_dsc_t* dsc) {
    char path[32];
    buildPath(suffix, path, sizeof(path));

    // Defense in depth: build() already disables these buttons when the
    // camera isn't READY (see below), but a defensive re-check here means
    // this function is correct on its own terms too, not just because
    // the caller happens to gate it. F6 requirement 6: a missing/failed
    // camera "blocks photo steps," with a camera-specific message — not
    // the generic, SD-flavored one below, which is now only reachable for
    // an actual SD failure with a healthy camera.
    if (!camera::isReady()) {
        components::showToast("Camera Problem. Photos cannot be taken right now.", components::ToastSeverity::Error,
                               4000);
        return;
    }

    camera::CaptureResult result = camera::captureToFile(path, camera::CAPTURE_MODE_PHOTO);
    if (!result.success) {
        // result.error is a diagnostic string ("SD write failed", "camera
        // not ready", ...) — logged, never shown; see LANGUAGE.md's
        // error standard and PROTOCOL.md's own "SD initialization
        // failed" -> plain-language example this message matches.
        Serial.printf("[photos] capture to %s failed: %s\n", path, result.error);
        *savedFlag = false;
        components::showToast("Photos cannot be saved. Storage is not available.", components::ToastSeverity::Error,
                               4000);
        judging::save();
        refreshSlot(slotContainer, false, buf, dsc, path);
        return;
    }

    *savedFlag = true;
    judging::save();
    refreshSlot(slotContainer, true, buf, dsc, path);
}

void onTakeCarPhoto(lv_event_t*) {
    capture("car", &judging::current().carPhotoSaved, g_carSlot, &g_carPreviewBuf, &g_carPreviewDsc);
}
void onTakeSheetPhoto(lv_event_t*) {
    capture("sheet", &judging::current().sheetPhotoSaved, g_sheetSlot, &g_sheetPreviewBuf, &g_sheetPreviewDsc);
}

void buildSlot(lv_obj_t* content, const char* title, bool saved, lv_obj_t** outContainer, uint8_t** buf,
               lv_img_dsc_t* dsc, const char* suffix, lv_event_cb_t onCapture, bool cameraReady) {
    lv_obj_t* wrap = lv_obj_create(content);
    lv_obj_remove_style_all(wrap);
    lv_obj_add_style(wrap, theme::card(), 0);
    lv_obj_set_style_pad_all(wrap, 12, 0);
    lv_obj_set_size(wrap, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wrap, 8, 0);

    lv_obj_t* titleLabel = lv_label_create(wrap);
    lv_obj_add_style(titleLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(titleLabel, &ui_font_plex_600_23, 0);
    lv_label_set_text(titleLabel, title);

    lv_obj_t* slotContainer = lv_obj_create(wrap);
    lv_obj_remove_style_all(slotContainer);
    lv_obj_add_style(slotContainer, theme::raisedSurface(), 0);
    lv_obj_set_size(slotContainer, PREVIEW_BOX_W, PREVIEW_BOX_W * ASSUMED_CAPTURE_H / ASSUMED_CAPTURE_W);
    lv_obj_clear_flag(slotContainer, LV_OBJ_FLAG_SCROLLABLE);
    *outContainer = slotContainer;

    char path[32];
    buildPath(suffix, path, sizeof(path));
    refreshSlot(slotContainer, saved, buf, dsc, path);

    lv_obj_t* btn = components::secondaryButton(wrap, saved ? "Retake" : "Take Photo", 200);
    lv_obj_add_event_cb(btn, onCapture, LV_EVENT_CLICKED, nullptr);
    // F6 requirement 6: a missing/failed camera blocks photo steps —
    // disabled up front, not just a reactive error after the tap (see
    // capture()'s own defensive re-check, which stays for the case this
    // screen was already open when the camera failed mid-session).
    components::setEnabled(btn, cameraReady);
}

void onContinue(lv_event_t*) {
    storage::DraftCar& car = judging::current();
    if (!car.carPhotoSaved || !car.sheetPhotoSaved) {
        components::showToast("Both photos are required before continuing.", components::ToastSeverity::Error,
                               3000);
        return;
    }
    car.furthestStep = storage::DraftStep::Review;
    judging::save();
    screen_manager::push(ReviewScreen::create);
}

}  // namespace

void PhotosScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 12, 0);

    storage::DraftCar& car = judging::current();

    bool cameraReady = camera::isReady();
    if (!cameraReady) {
        components::alertBanner(
            content, components::AlertSeverity::Critical,
            "Camera Problem. Photos cannot be taken on this device right now — try judging this car on another "
            "handheld instead.");
    }

    lv_obj_t* row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    buildSlot(row, "Vehicle Photo", car.carPhotoSaved, &g_carSlot, &g_carPreviewBuf, &g_carPreviewDsc, "car",
              onTakeCarPhoto, cameraReady);
    buildSlot(row, "Judge Sheet Photo", car.sheetPhotoSaved, &g_sheetSlot, &g_sheetPreviewBuf, &g_sheetPreviewDsc,
              "sheet", onTakeSheetPhoto, cameraReady);

    lv_obj_t* continueBtn = components::primaryButton(content, "Continue to Review", 320);
    lv_obj_add_event_cb(continueBtn, onContinue, LV_EVENT_CLICKED, nullptr);
}

void PhotosScreen::teardown() {
    freePreview(&g_carPreviewBuf);
    freePreview(&g_sheetPreviewBuf);
    g_carSlot = nullptr;
    g_sheetSlot = nullptr;
}

Screen* PhotosScreen::create(void* /*arg*/) { return new PhotosScreen(); }

}  // namespace ui::screens
