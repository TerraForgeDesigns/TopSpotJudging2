#include "component_demo_screen.h"

#include <cstdio>
#include <cstring>

#include "../components/alert_banner.h"
#include "../components/button.h"
#include "../components/card.h"
#include "../components/checklist_row.h"
#include "../components/list_row.h"
#include "../components/modal_confirm.h"
#include "../components/numeric_keypad.h"
#include "../components/photo_slot_row.h"
#include "../components/score_grid.h"
#include "../components/score_row.h"
#include "../components/searchable_selector.h"
#include "../components/text_keyboard.h"
#include "../components/toast.h"
#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::screens {

namespace {

void sectionTitle(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_add_style(label, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(label, &ui_font_archivo_700_30, 0);
    lv_obj_set_style_pad_top(label, 20, 0);
    lv_label_set_text(label, text);
}

void onThemeDarkClicked(lv_event_t*) { theme::setMode(theme::Mode::Dark); }
void onThemeDaylightClicked(lv_event_t*) { theme::setMode(theme::Mode::Daylight); }

void onNumericKey(void* ctx, char key) {
    auto* label = static_cast<lv_obj_t*>(ctx);
    static char buf[16] = "";
    size_t len = strlen(buf);
    if (key == 'C') {
        buf[0] = '\0';
    } else if (key == 'B') {
        if (len > 0) buf[len - 1] = '\0';
    } else if (len < sizeof(buf) - 1) {
        buf[len] = key;
        buf[len + 1] = '\0';
    }
    lv_label_set_text(label, buf[0] != '\0' ? buf : "(tap a key)");
}

void onScoreChanged(void* ctx, int value) {
    auto* label = static_cast<lv_obj_t*>(ctx);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_label_set_text(label, buf);
}

void onOpenTextKeyboard(lv_event_t* e) {
    auto* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const char* current = lv_label_get_text(label);
    components::textKeyboardOverlay(
        current, "Participant name", 40,
        [](void* ctx, const char* text, bool accepted) {
            if (!accepted) return;
            lv_label_set_text(static_cast<lv_obj_t*>(ctx), text);
        },
        label);
}

void onOpenNumericKeyboard(lv_event_t* e) {
    auto* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const char* current = lv_label_get_text(label);
    components::numericKeyboardOverlay(
        current, "Year", 4,
        [](void* ctx, const char* text, bool accepted) {
            if (!accepted) return;
            lv_label_set_text(static_cast<lv_obj_t*>(ctx), text);
        },
        label);
}

const char* DEMO_MAKES[] = {
    "Buick",  "Cadillac", "Chevrolet", "Chrysler", "Datsun",     "Dodge",     "Ford",
    "GMC",    "Honda",    "Jeep",      "Mercury",  "Oldsmobile", "Plymouth",  "Pontiac",
    "Toyota", "Triumph",  "Volkswagen",
};

void onSelectorPick(void* ctx, const char* text, int /*index*/) {
    auto* label = static_cast<lv_obj_t*>(ctx);
    lv_label_set_text(label, text);
}

void onChecklistChanged(void* ctx, bool checked) {
    auto* label = static_cast<lv_obj_t*>(ctx);
    lv_label_set_text(label, checked ? "Nominated" : "Not nominated");
}

void onModalDestructive(lv_event_t*) {
    components::modalConfirm(
        "Discard this set of scores?",
        "Car 231 already has an accepted set of scores. Discarding this one instead cannot be undone.", "Discard",
        true, [](void*, bool) {}, nullptr);
}

void onModalPlain(lv_event_t*) {
    components::modalConfirm(
        "Finish this car?", "042 will be marked judged once you continue.", "Finish", false, [](void*, bool) {},
        nullptr);
}

void onToastSuccess(lv_event_t*) { components::showToast("Saved.", components::ToastSeverity::Success); }
void onToastError(lv_event_t*) {
    components::showToast("Home Base could not be reached.", components::ToastSeverity::Error);
}

}  // namespace

void ComponentDemoScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 6, 0);

    // ---- Theme toggle ----
    sectionTitle(content, "Theme");
    lv_obj_t* themeRow = lv_obj_create(content);
    lv_obj_remove_style_all(themeRow);
    lv_obj_set_size(themeRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(themeRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(themeRow, 10, 0);
    lv_obj_clear_flag(themeRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(components::secondaryButton(themeRow, "Dark"), onThemeDarkClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(components::secondaryButton(themeRow, "Daylight"), onThemeDaylightClicked, LV_EVENT_CLICKED,
                         nullptr);

    // ---- Buttons ----
    sectionTitle(content, "Buttons");
    lv_obj_t* btnRow = lv_obj_create(content);
    lv_obj_remove_style_all(btnRow);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btnRow, 10, 0);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    components::primaryButton(btnRow, "Primary");
    components::secondaryButton(btnRow, "Secondary");
    lv_obj_t* disabledBtn = components::primaryButton(btnRow, "Disabled");
    components::setEnabled(disabledBtn, false);

    // ---- Numeric keypad ----
    sectionTitle(content, "Numeric Keypad");
    lv_obj_t* keypadValue = lv_label_create(content);
    lv_obj_add_style(keypadValue, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(keypadValue, &ui_font_archivo_800_46, 0);
    lv_label_set_text(keypadValue, "(tap a key)");
    components::numericKeypad(content, onNumericKey, keypadValue);

    // ---- Score row (1-10) ----
    sectionTitle(content, "Score Row (1-10)");
    lv_obj_t* scoreRowValue = lv_label_create(content);
    lv_obj_add_style(scoreRowValue, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(scoreRowValue, &ui_font_archivo_700_30, 0);
    lv_label_set_text(scoreRowValue, "-");
    components::scoreRow(content, 10, 0, onScoreChanged, scoreRowValue);

    // ---- Score grid (1-25) ----
    sectionTitle(content, "Score Grid (1-25)");
    lv_obj_t* scoreGridValue = lv_label_create(content);
    lv_obj_add_style(scoreGridValue, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(scoreGridValue, &ui_font_archivo_700_30, 0);
    lv_label_set_text(scoreGridValue, "-");
    components::scoreGrid(content, 0, onScoreChanged, scoreGridValue);

    // ---- Text / numeric keyboard overlays ----
    sectionTitle(content, "Text & Numeric Keyboard");
    lv_obj_t* textResult = lv_label_create(content);
    lv_obj_add_style(textResult, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(textResult, &ui_font_plex_400_16, 0);
    lv_label_set_text(textResult, "Marcus Webb");
    lv_obj_t* yearResult = lv_label_create(content);
    lv_obj_add_style(yearResult, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(yearResult, &ui_font_plex_400_16, 0);
    lv_label_set_text(yearResult, "1969");
    lv_obj_t* kbRow = lv_obj_create(content);
    lv_obj_remove_style_all(kbRow);
    lv_obj_set_size(kbRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(kbRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(kbRow, 10, 0);
    lv_obj_clear_flag(kbRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(components::secondaryButton(kbRow, "Edit Name"), onOpenTextKeyboard, LV_EVENT_CLICKED,
                         textResult);
    lv_obj_add_event_cb(components::secondaryButton(kbRow, "Edit Year"), onOpenNumericKeyboard, LV_EVENT_CLICKED,
                         yearResult);

    // ---- Searchable selector ----
    sectionTitle(content, "Searchable Selector");
    lv_obj_t* selectorResult = lv_label_create(content);
    lv_obj_add_style(selectorResult, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(selectorResult, &ui_font_plex_400_16, 0);
    lv_label_set_text(selectorResult, "(nothing selected)");
    lv_obj_t* selectorHolder = lv_obj_create(content);
    lv_obj_remove_style_all(selectorHolder);
    lv_obj_set_size(selectorHolder, LV_PCT(100), 260);
    lv_obj_clear_flag(selectorHolder, LV_OBJ_FLAG_SCROLLABLE);
    components::searchableSelector(selectorHolder, DEMO_MAKES, sizeof(DEMO_MAKES) / sizeof(DEMO_MAKES[0]),
                                    "Search make", onSelectorPick, selectorResult);

    // ---- Checklist row ----
    sectionTitle(content, "Checklist Row");
    lv_obj_t* checklistResult = lv_label_create(content);
    lv_obj_add_style(checklistResult, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(checklistResult, &ui_font_plex_400_14, 0);
    lv_label_set_text(checklistResult, "Not nominated");
    components::checklistRow(content, "Nominate for Best Paint", false, onChecklistChanged, checklistResult);

    // ---- Photo slot row ----
    sectionTitle(content, "Photo Slot Row");
    components::photoSlotRow(content, "Car Photo", components::PhotoSlotState::Taken, "Retake", nullptr, nullptr);
    components::photoSlotRow(content, "Judge Sheet", components::PhotoSlotState::Missing, "Take Photo", nullptr,
                              nullptr);

    // ---- Alert banner ----
    sectionTitle(content, "Alert Banner");
    components::alertBanner(content, components::AlertSeverity::Info,
                             "042 is already scored. Confirm this is a re-check before continuing.");
    components::alertBanner(content, components::AlertSeverity::Critical,
                             "Home Base could not be reached. Check that this device is connected to the show Wi-Fi.");

    // ---- List row ----
    sectionTitle(content, "List Row");
    lv_obj_t* listCard = components::card(content);
    components::listRow(listCard, "hh-1", "Up to Date", components::RowDot::Good, nullptr, nullptr);
    components::listRow(listCard, "hh-2", "Updating", components::RowDot::Pending, nullptr, nullptr);
    components::listRow(listCard, "hh-3", "Not Connected", components::RowDot::Bad, nullptr, nullptr);
    components::listRow(listCard, "Settings", nullptr, components::RowDot::None, nullptr, nullptr);

    // ---- Card ----
    sectionTitle(content, "Card");
    lv_obj_t* sampleCard = components::card(content);
    lv_obj_t* cardTitle = lv_label_create(sampleCard);
    lv_obj_add_style(cardTitle, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(cardTitle, &ui_font_plex_600_23, 0);
    lv_label_set_text(cardTitle, "Car 042");
    lv_obj_t* cardBody = lv_label_create(sampleCard);
    lv_obj_add_style(cardBody, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(cardBody, &ui_font_plex_400_16, 0);
    lv_label_set_text(cardBody, "1969 Chevrolet Camaro SS");

    // ---- Modal confirm ----
    sectionTitle(content, "Modal Confirm");
    lv_obj_t* modalRow = lv_obj_create(content);
    lv_obj_remove_style_all(modalRow);
    lv_obj_set_size(modalRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(modalRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(modalRow, 10, 0);
    lv_obj_clear_flag(modalRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(components::secondaryButton(modalRow, "Plain Confirm"), onModalPlain, LV_EVENT_CLICKED,
                         nullptr);
    lv_obj_add_event_cb(components::secondaryButton(modalRow, "Destructive Confirm"), onModalDestructive,
                         LV_EVENT_CLICKED, nullptr);

    // ---- Toast ----
    sectionTitle(content, "Toast");
    lv_obj_t* toastRow = lv_obj_create(content);
    lv_obj_remove_style_all(toastRow);
    lv_obj_set_size(toastRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(toastRow, 24, 0);  // room to see toasts land at the screen bottom
    lv_obj_set_flex_flow(toastRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(toastRow, 10, 0);
    lv_obj_clear_flag(toastRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(components::secondaryButton(toastRow, "Success Toast"), onToastSuccess, LV_EVENT_CLICKED,
                         nullptr);
    lv_obj_add_event_cb(components::secondaryButton(toastRow, "Error Toast"), onToastError, LV_EVENT_CLICKED,
                         nullptr);
}

Screen* ComponentDemoScreen::create(void* /*arg*/) { return new ComponentDemoScreen(); }

}  // namespace ui::screens
