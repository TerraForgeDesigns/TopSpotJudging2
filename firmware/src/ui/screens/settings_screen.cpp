#include "settings_screen.h"

#include <cstdint>
#include <cstring>

#include <cstdio>
#include <cstdlib>

#include "../components/button.h"
#include "../components/list_row.h"
#include "../components/numeric_keypad_overlay.h"
#include "../components/text_keyboard.h"
#include "../fonts/fonts.h"
#include "../theme.h"
#include "storage/settings.h"

namespace ui::screens {

namespace {

// Which field a tap on a row is editing — passed through the keyboard
// overlay's void* ctx since it (like every F2 component) is a plain C
// callback, not a capturing lambda.
enum class Field { HandheldLabel, JudgeName, WifiSsid, WifiPassword, HomeBaseAddress };

// network::sync's periodic trigger doubles this on a miss, capped at
// 15 minutes (see storage/settings.h) — a base already at or above the
// cap would never actually back off, so it's validated here, not just
// silently clamped.
constexpr int MIN_SYNC_INTERVAL_MINUTES = 1;
constexpr int MAX_SYNC_INTERVAL_MINUTES = 15;

bool isValidSyncInterval(const char* text) {
    if (text[0] == '\0') return false;
    int minutes = atoi(text);
    return minutes >= MIN_SYNC_INTERVAL_MINUTES && minutes <= MAX_SYNC_INTERVAL_MINUTES;
}

// Module-static, not a build()-local — the row callbacks below fire
// later, asynchronously, after build() has already returned. A pointer
// into a stack-local Settings would be dangling by the time a judge
// actually taps a row; this instead lives for as long as the firmware
// runs (screen_manager's one-screen-at-a-time design means only one
// Settings screen is ever alive to read or write it).
storage::Settings g_settings;

void onFieldSaved(void* ctx, const char* text, bool accepted) {
    if (!accepted) return;
    Field field = static_cast<Field>(reinterpret_cast<intptr_t>(ctx));

    switch (field) {
        case Field::HandheldLabel: strncpy(g_settings.handheldLabel, text, sizeof(g_settings.handheldLabel) - 1); break;
        case Field::JudgeName: strncpy(g_settings.judgeName, text, sizeof(g_settings.judgeName) - 1); break;
        case Field::WifiSsid: strncpy(g_settings.wifiSsid, text, sizeof(g_settings.wifiSsid) - 1); break;
        case Field::WifiPassword: strncpy(g_settings.wifiPassword, text, sizeof(g_settings.wifiPassword) - 1); break;
        case Field::HomeBaseAddress:
            strncpy(g_settings.homeBaseAddress, text, sizeof(g_settings.homeBaseAddress) - 1);
            break;
    }
    storage::saveSettings(g_settings);
    screen_manager::refresh();
}

void editField(Field field, const char* currentValue, const char* placeholder) {
    components::textKeyboardOverlay(currentValue, placeholder, 63, onFieldSaved,
                                     reinterpret_cast<void*>(static_cast<intptr_t>(field)));
}

void onEditHandheldLabel(void* ctx) { editField(Field::HandheldLabel, static_cast<const char*>(ctx), "Handheld label"); }
void onEditJudgeName(void* ctx) { editField(Field::JudgeName, static_cast<const char*>(ctx), "Judge name"); }
void onEditWifiSsid(void* ctx) { editField(Field::WifiSsid, static_cast<const char*>(ctx), "Show Wi-Fi name"); }
void onEditWifiPassword(void* ctx) { editField(Field::WifiPassword, static_cast<const char*>(ctx), "Show Wi-Fi password"); }
void onEditHomeBaseAddress(void* ctx) {
    editField(Field::HomeBaseAddress, static_cast<const char*>(ctx), "Home Base address");
}

void onSyncIntervalSaved(void* /*ctx*/, const char* text, bool accepted) {
    if (!accepted) return;
    g_settings.syncIntervalSeconds = atoi(text) * 60;
    storage::saveSettings(g_settings);
    screen_manager::refresh();
}

void onEditSyncInterval(void* /*ctx*/) {
    char current[4];
    snprintf(current, sizeof(current), "%d", g_settings.syncIntervalSeconds / 60);
    components::numericKeypadOverlay(current, "Minutes", /*maxLen=*/2, /*autoAcceptLen=*/0, isValidSyncInterval,
                                      "Enter 1 to 15 minutes.", onSyncIntervalSaved, nullptr);
}

void onThemeDark(lv_event_t*) {
    g_settings.theme = storage::ThemeChoice::Dark;
    storage::saveSettings(g_settings);
    theme::setMode(theme::Mode::Dark);
}

void onThemeDaylight(lv_event_t*) {
    g_settings.theme = storage::ThemeChoice::Daylight;
    storage::saveSettings(g_settings);
    theme::setMode(theme::Mode::Daylight);
}

}  // namespace

void SettingsScreen::build(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 8, 0);

    storage::loadSettings(&g_settings);

    // list_row.h's callback takes a plain void* ctx with no distinction
    // between "field being edited" and "current value to prefill" — two
    // pieces of information through a components::ListRowCallback's one
    // void* slot. Passing the current value (a stable pointer into the
    // module-static g_settings above) and closing over which field via a
    // distinct wrapper function per row is simpler here than inventing a
    // heap-allocated context struct for five near-identical rows.
    components::listRow(content, "Handheld Label", g_settings.handheldLabel, components::RowDot::None,
                         onEditHandheldLabel, g_settings.handheldLabel);
    components::listRow(content, "Judge Name", g_settings.judgeName, components::RowDot::None, onEditJudgeName,
                         g_settings.judgeName);
    components::listRow(content, "Show Wi-Fi Name", g_settings.wifiSsid, components::RowDot::None, onEditWifiSsid,
                         g_settings.wifiSsid);
    components::listRow(content, "Show Wi-Fi Password",
                         g_settings.wifiPassword[0] != '\0' ? "********" : "", components::RowDot::None,
                         onEditWifiPassword, g_settings.wifiPassword);
    components::listRow(content, "Home Base Address", g_settings.homeBaseAddress, components::RowDot::None,
                         onEditHomeBaseAddress, g_settings.homeBaseAddress);

    char intervalBuf[32];
    snprintf(intervalBuf, sizeof(intervalBuf), "%d minutes", g_settings.syncIntervalSeconds / 60);
    components::listRow(content, "Update Check Interval", intervalBuf, components::RowDot::None, onEditSyncInterval,
                         nullptr);

    lv_obj_t* themeLabel = lv_label_create(content);
    lv_obj_add_style(themeLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(themeLabel, &ui_font_plex_500_19, 0);
    lv_obj_set_style_pad_top(themeLabel, 12, 0);
    lv_label_set_text(themeLabel, "THEME");

    lv_obj_t* themeRow = lv_obj_create(content);
    lv_obj_remove_style_all(themeRow);
    lv_obj_set_size(themeRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(themeRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(themeRow, 10, 0);
    lv_obj_clear_flag(themeRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(components::secondaryButton(themeRow, "Dark"), onThemeDark, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(components::secondaryButton(themeRow, "Daylight"), onThemeDaylight, LV_EVENT_CLICKED,
                         nullptr);

    lv_obj_t* hint = lv_label_create(content);
    lv_obj_add_style(hint, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(hint, &ui_font_plex_400_14, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_pad_top(hint, 16, 0);
    lv_label_set_text(hint,
                       "These can also be edited by taking the memory card out and opening "
                       "settings.txt in any text editor - useful if a setting is wrong and this "
                       "screen isn't reachable.");
}

Screen* SettingsScreen::create(void* /*arg*/) { return new SettingsScreen(); }

}  // namespace ui::screens
