#include "ui_screens.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "car_status_store.h"
#include "judging_model.h"
#include "lvgl.h"
#include "p4_camera.h"
#include "p4_battery.h"
#include "p4_network.h"
#include "p4_storage.h"
#include "record_store.h"
#include "show_store.h"
#include "ui_theme.h"

static const char *TAG = "top_spot_ui";

typedef enum {
    TS_SCREEN_HOME,
    TS_SCREEN_SHOW_STATUS,
    TS_SCREEN_CARS,
    TS_SCREEN_ENTRY_BLOCKED,
    TS_SCREEN_DETAILS,
    TS_SCREEN_JUDGE,
    TS_SCREEN_AWARDS,
    TS_SCREEN_PHOTOS,
    TS_SCREEN_CAMERA,
    TS_SCREEN_REVIEW,
    TS_SCREEN_SAVED,
} ts_screen_id_t;

typedef enum {
    TS_CAMERA_ACTION_CAPTURE,
    TS_CAMERA_ACTION_CLOSE,
} ts_camera_action_t;

enum {
    TS_CARS_ROWS_PER_PAGE = 6,
    TS_CARS_ROW_HEIGHT = 43,
    TS_CARS_ROW_GAP = 5,
};

typedef struct {
    ts_camera_action_t action;
    top_spot_photo_type_t photo_type;
    char entry_number[16];
} ts_camera_job_t;

typedef struct {
    ts_camera_action_t action;
    top_spot_photo_type_t photo_type;
    esp_err_t result;
    char path[128];
    size_t file_size;
} ts_camera_result_t;

typedef struct {
    top_spot_app_state_t state;
    ts_screen_id_t screen;
    lv_obj_t *keyboard;
    lv_obj_t *detail_fields[5];
    lv_obj_t *score_buttons[TOP_SPOT_MAX_SCORE_RANGE];
    lv_obj_t *score_hint;
    lv_obj_t *camera_status;
    lv_obj_t *camera_back_button;
    lv_obj_t *camera_capture_button;
    lv_timer_t *camera_result_timer;
    lv_timer_t *network_status_timer;
    lv_timer_t *battery_status_timer;
    lv_obj_t *battery_indicator;
    lv_obj_t *wifi_indicator;
    lv_obj_t *homebase_indicator;
    lv_obj_t *home_wifi_value;
    lv_obj_t *home_homebase_value;
    lv_obj_t *home_storage_value;
    lv_obj_t *home_show_value;
    lv_obj_t *home_show_help;
    lv_obj_t *home_saved_value;
    lv_obj_t *home_sync_value;
    lv_obj_t *home_enter_button;
    lv_obj_t *home_status_button;
    lv_obj_t *cars_rows[TS_CARS_ROWS_PER_PAGE];
    lv_obj_t *cars_page_label;
    lv_obj_t *cars_lookup_field;
    QueueHandle_t camera_result_queue;
    bool camera_busy;
    bool show_ready_logged;
    bool state_prepared;
    top_spot_car_filter_t cars_filter;
    int cars_page;
    char blocked_title[48];
    char blocked_message[160];
    top_spot_photo_type_t active_photo_type;
} ts_ui_t;

static ts_ui_t s_ui;

static void show_screen(ts_screen_id_t screen);
static void on_keyboard_ready(lv_event_t *event);
static lv_obj_t *add_field(lv_obj_t *parent, const char *label, const char *value, int max_len, bool numeric);

static const char *wifi_icon_for_level(top_spot_wifi_level_t level)
{
    switch (level) {
    case TOP_SPOT_WIFI_STRONG:
        return "||||";
    case TOP_SPOT_WIFI_GOOD:
        return "|||.";
    case TOP_SPOT_WIFI_MODERATE:
        return "||..";
    case TOP_SPOT_WIFI_WEAK:
        return "|...";
    case TOP_SPOT_WIFI_DISCONNECTED:
    default:
        return "----";
    }
}

static void update_network_status_labels(void)
{
    top_spot_network_status_t status = {0};
    top_spot_network_get_status(&status);
    bool show_ready = s_ui.state.show.category_count > 0;

    if (s_ui.wifi_indicator != NULL) {
        char wifi[48];
        if (status.wifi_connected) {
            snprintf(wifi, sizeof(wifi), "%s Wi-Fi %d dBm",
                     wifi_icon_for_level(status.wifi_level), status.rssi_dbm);
        } else {
            snprintf(wifi, sizeof(wifi), "%s Wi-Fi disconnected",
                     wifi_icon_for_level(TOP_SPOT_WIFI_DISCONNECTED));
        }
        lv_label_set_text(s_ui.wifi_indicator, wifi);
        lv_obj_set_style_text_color(s_ui.wifi_indicator,
                                    ts_color(status.wifi_connected ? TS_COLOR_GREEN : TS_COLOR_MUTED),
                                    LV_PART_MAIN);
    }

    if (s_ui.homebase_indicator != NULL) {
        const char *text = status.homebase_connected ? "Home Base connected" : "Home Base unavailable";
        lv_label_set_text(s_ui.homebase_indicator, text);
        lv_obj_set_style_text_color(s_ui.homebase_indicator,
                                    ts_color(status.homebase_connected ? TS_COLOR_GREEN : TS_COLOR_ACCENT),
                                    LV_PART_MAIN);
    }

    if (s_ui.home_wifi_value != NULL) {
        lv_label_set_text(s_ui.home_wifi_value, top_spot_network_wifi_text());
    }
    if (s_ui.home_homebase_value != NULL) {
        lv_label_set_text(s_ui.home_homebase_value, top_spot_network_homebase_text());
    }
    if (s_ui.home_storage_value != NULL) {
        lv_label_set_text(s_ui.home_storage_value, top_spot_storage_status_text());
    }
    if (s_ui.home_show_value != NULL) {
        lv_label_set_text(s_ui.home_show_value,
                          show_ready ? s_ui.state.show.show_name : "No Show Loaded");
    }
    if (s_ui.home_show_help != NULL) {
        lv_label_set_text(s_ui.home_show_help,
                          show_ready ? "Ready to enter vehicles" : "Waiting for Home Base show configuration");
        lv_obj_set_style_text_color(s_ui.home_show_help,
                                    ts_color(show_ready ? TS_COLOR_GREEN : TS_COLOR_MUTED),
                                    LV_PART_MAIN);
    }
    if (s_ui.home_enter_button != NULL) {
        if (show_ready) {
            lv_obj_clear_state(s_ui.home_enter_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_ui.home_enter_button, LV_STATE_DISABLED);
        }
    }
    if (show_ready && !s_ui.show_ready_logged) {
        ESP_LOGI(TAG, "UI show state ready: %s", s_ui.state.show.show_name);
        s_ui.show_ready_logged = true;
    }
    if (s_ui.home_saved_value != NULL) {
        char saved[32];
        snprintf(saved, sizeof(saved), "%d saved locally", s_ui.state.saved_count);
        lv_label_set_text(s_ui.home_saved_value, saved);
    }
    if (s_ui.home_sync_value != NULL) {
        lv_label_set_text(s_ui.home_sync_value, top_spot_network_sync_text());
    }
}

static void network_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_network_status_labels();
}

static void update_battery_status_label(void)
{
    if (s_ui.battery_indicator == NULL) return;
    top_spot_battery_status_t battery;
    top_spot_battery_get_status(&battery);
    uint32_t color = TS_COLOR_MUTED;
    if (!battery.valid) {
        lv_label_set_text(s_ui.battery_indicator, LV_SYMBOL_BATTERY_EMPTY " --%  --.--V");
    } else {
        const char *state = "";
        color = TS_COLOR_TEXT;
        switch (battery.level) {
        case TOP_SPOT_BATTERY_LOW:
            state = " Low";
            color = TS_COLOR_ACCENT;
            break;
        case TOP_SPOT_BATTERY_VERY_LOW:
            state = " Very low";
            color = TS_COLOR_RED;
            break;
        case TOP_SPOT_BATTERY_CRITICAL:
            state = " CRITICAL";
            color = TS_COLOR_RED;
            break;
        default:
            break;
        }
        const char *icon = battery.percent > 80 ? LV_SYMBOL_BATTERY_FULL :
                           battery.percent > 50 ? LV_SYMBOL_BATTERY_3 :
                           battery.percent > 20 ? LV_SYMBOL_BATTERY_2 :
                           battery.percent > 5 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        int voltage_cv = (battery.voltage_mv + 5) / 10;
        lv_label_set_text_fmt(s_ui.battery_indicator, "%s %d%%  %d.%02dV%s",
                             icon, battery.percent, voltage_cv / 100, voltage_cv % 100, state);
    }
    lv_obj_set_style_text_color(s_ui.battery_indicator, ts_color(color), LV_PART_MAIN);
}

static void battery_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_battery_status_label();
}

static void add_battery_indicator(lv_obj_t *parent)
{
    s_ui.battery_indicator = ts_label(parent, "", &lv_font_montserrat_16, TS_COLOR_MUTED);
    update_battery_status_label();
}

static void add_network_status_bar(lv_obj_t *page)
{
    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 14, LV_PART_MAIN);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.wifi_indicator = ts_label(bar, "---- Wi-Fi disconnected", &lv_font_montserrat_16, TS_COLOR_MUTED);
    s_ui.homebase_indicator = ts_label(bar, "Home Base unavailable", &lv_font_montserrat_16, TS_COLOR_ACCENT);
    add_battery_indicator(bar);
    update_network_status_labels();
}

static lv_obj_t *make_page(lv_obj_t *screen)
{
    lv_obj_t *page = ts_page(screen);
    add_network_status_bar(page);
    return page;
}

static void set_text(char *dest, size_t dest_size, lv_obj_t *textarea)
{
    const char *value = lv_textarea_get_text(textarea);
    if (value == NULL) {
        value = "";
    }
    snprintf(dest, dest_size, "%s", value);
}

static void add_header(lv_obj_t *page, const char *eyebrow, const char *title, const char *subtitle)
{
    lv_obj_t *header = lv_obj_create(page);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(header, 6, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *eyebrow_label = ts_label(header, eyebrow, &lv_font_montserrat_16, TS_COLOR_CYAN);
    lv_label_set_text_static(eyebrow_label, eyebrow);

    lv_obj_t *title_label = ts_label(header, title, &lv_font_montserrat_32, TS_COLOR_TEXT);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title_label, LV_PCT(100));

    if (subtitle != NULL && subtitle[0] != '\0') {
        lv_obj_t *subtitle_label = ts_label(header, subtitle, &lv_font_montserrat_20, TS_COLOR_MUTED);
        lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(subtitle_label, LV_PCT(100));
    }
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *add_value_row(lv_obj_t *parent, const char *label, const char *value)
{
    lv_obj_t *row = make_row(parent);
    lv_obj_t *name = ts_label(row, label, &lv_font_montserrat_16, TS_COLOR_MUTED);
    lv_obj_set_width(name, 230);

    lv_obj_t *val = ts_label(row, (value != NULL && value[0] != '\0') ? value : "Not entered",
                             &lv_font_montserrat_16, TS_COLOR_TEXT);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(val, 440);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    return val;
}

static void add_nav(lv_obj_t *page, const char *back_text, lv_event_cb_t back_cb,
                    const char *next_text, lv_event_cb_t next_cb)
{
    lv_obj_t *row = make_row(page);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (back_text != NULL) {
        lv_obj_t *back = ts_button(row, back_text, 180, false);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    }
    if (next_text != NULL) {
        lv_obj_t *next = ts_button(row, next_text, 220, true);
        lv_obj_add_event_cb(next, next_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void on_go_home(lv_event_t *event)
{
    (void)event;
    show_screen(TS_SCREEN_HOME);
}

static void on_show_status(lv_event_t *event)
{
    (void)event;
    show_screen(TS_SCREEN_SHOW_STATUS);
}

static void on_cars(lv_event_t *event)
{
    (void)event;
    s_ui.cars_page = 0;
    show_screen(TS_SCREEN_CARS);
}

static void block_entry(const char *title, const char *message)
{
    snprintf(s_ui.blocked_title, sizeof(s_ui.blocked_title), "%s", title);
    snprintf(s_ui.blocked_message, sizeof(s_ui.blocked_message), "%s", message);
    show_screen(TS_SCREEN_ENTRY_BLOCKED);
}

static bool check_entry_can_be_judged(void)
{
    if (s_ui.state.car_cache.loaded && s_ui.state.car_cache.car_count > 0) {
        top_spot_car_status_t car;
        if (!top_spot_car_status_find_entry(s_ui.state.current.entry_number, &car)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "Entry %s is not in the last synced Home Base roster.",
                     s_ui.state.current.entry_number);
            block_entry("Entry Not Found", msg);
            return false;
        }
        snprintf(s_ui.state.current.entry_number, sizeof(s_ui.state.current.entry_number), "%s",
                 car.entry_number);
        if (strcmp(car.status, TOP_SPOT_CAR_STATUS_JUDGED) == 0 ||
            strcmp(car.status, TOP_SPOT_CAR_STATUS_CONFLICT) == 0) {
            char msg[160];
            snprintf(msg, sizeof(msg), "Entry %s is already judged. Source: %s",
                     car.entry_number,
                     car.judged_source[0] != '\0' ? car.judged_source : "Home Base");
            block_entry("Already Judged", msg);
            return false;
        }
        if (strcmp(car.status, TOP_SPOT_CAR_STATUS_PENDING_SYNC) == 0) {
            char msg[160];
            snprintf(msg, sizeof(msg), "Entry %s already has a local record waiting to sync.",
                     car.entry_number);
            block_entry("Pending Sync", msg);
            return false;
        }
    } else {
        ESP_LOGW(TAG, "No cached roster available; entry validation falling back to Home Base upload response");
    }
    return true;
}

static void on_details(lv_event_t *event)
{
    (void)event;
    if (s_ui.state.show.category_count == 0) {
        ESP_LOGW(TAG, "Enter Car unavailable: no active show loaded");
        show_screen(TS_SCREEN_HOME);
        return;
    }
    top_spot_start_new_car(&s_ui.state);
    show_screen(TS_SCREEN_DETAILS);
}

static void show_home(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *brand = ts_label(page, "Top Spot Judging", &lv_font_montserrat_32, TS_COLOR_TEXT);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(brand, LV_PCT(100));

    lv_obj_t *subtitle = ts_label(page, "Local handheld judging", &lv_font_montserrat_24, TS_COLOR_CYAN);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(subtitle, LV_PCT(100));

    lv_obj_t *card = ts_card(page);
    lv_obj_set_width(card, 560);
    s_ui.home_show_value = add_value_row(card, "Show",
                                         s_ui.state.show.category_count > 0 ? s_ui.state.show.show_name : "No Show Loaded");
    s_ui.home_wifi_value = add_value_row(card, "Wi-Fi", top_spot_network_wifi_text());
    s_ui.home_homebase_value = add_value_row(card, "Home Base", top_spot_network_homebase_text());
    s_ui.home_storage_value = add_value_row(card, "Storage", top_spot_storage_status_text());

    char saved[32];
    snprintf(saved, sizeof(saved), "%d saved locally", s_ui.state.saved_count);
    s_ui.home_saved_value = add_value_row(card, "Status", saved);
    s_ui.home_sync_value = add_value_row(card, "Sync", top_spot_network_sync_text());

    s_ui.home_show_help = ts_label(page,
                                   s_ui.state.show.category_count > 0
                                       ? "Ready to enter vehicles"
                                       : "Waiting for Home Base show configuration",
                                   &lv_font_montserrat_16,
                                   s_ui.state.show.category_count > 0 ? TS_COLOR_GREEN : TS_COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.home_show_help, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(s_ui.home_show_help, LV_PCT(100));

    lv_obj_t *button = ts_button(page, "Enter Car", 280, true);
    s_ui.home_enter_button = button;
    if (s_ui.state.show.category_count == 0) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(button, on_details, LV_EVENT_CLICKED, NULL);

    lv_obj_t *row = make_row(page);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *status = ts_button(row, "Show Status", 220, false);
    s_ui.home_status_button = status;
    lv_obj_add_event_cb(status, on_show_status, LV_EVENT_CLICKED, NULL);
}

static const char *display_status(const char *status)
{
    if (strcmp(status, TOP_SPOT_CAR_STATUS_JUDGED) == 0) {
        return "JUDGED";
    }
    if (strcmp(status, TOP_SPOT_CAR_STATUS_PENDING_SYNC) == 0) {
        return "PENDING SYNC";
    }
    if (strcmp(status, TOP_SPOT_CAR_STATUS_CONFLICT) == 0) {
        return "CONFLICT";
    }
    return "UNJUDGED";
}

static uint32_t status_color(const char *status)
{
    if (strcmp(status, TOP_SPOT_CAR_STATUS_JUDGED) == 0) {
        return TS_COLOR_GREEN;
    }
    if (strcmp(status, TOP_SPOT_CAR_STATUS_PENDING_SYNC) == 0) {
        return TS_COLOR_ACCENT;
    }
    if (strcmp(status, TOP_SPOT_CAR_STATUS_CONFLICT) == 0) {
        return TS_COLOR_ACCENT;
    }
    return TS_COLOR_MUTED;
}

static const char *cars_filter_name(top_spot_car_filter_t filter)
{
    switch (filter) {
    case TOP_SPOT_CAR_FILTER_JUDGED:
        return "Judged";
    case TOP_SPOT_CAR_FILTER_UNJUDGED:
        return "Unjudged";
    case TOP_SPOT_CAR_FILTER_PENDING:
        return "Pending";
    case TOP_SPOT_CAR_FILTER_ALL:
    default:
        return "All";
    }
}

static void on_cars_filter(lv_event_t *event)
{
    s_ui.cars_filter = (top_spot_car_filter_t)(intptr_t)lv_event_get_user_data(event);
    s_ui.cars_page = 0;
    show_screen(TS_SCREEN_CARS);
}

static void on_cars_prev(lv_event_t *event)
{
    (void)event;
    if (s_ui.cars_page > 0) {
        s_ui.cars_page--;
    }
    show_screen(TS_SCREEN_CARS);
}

static void on_cars_next(lv_event_t *event)
{
    (void)event;
    int visible = top_spot_car_status_visible_count(s_ui.cars_filter);
    int max_page = visible > 0 ? (visible - 1) / TS_CARS_ROWS_PER_PAGE : 0;
    if (s_ui.cars_page < max_page) {
        s_ui.cars_page++;
    }
    show_screen(TS_SCREEN_CARS);
}

static void on_lookup(lv_event_t *event)
{
    (void)event;
    const char *entry = lv_textarea_get_text(s_ui.cars_lookup_field);
    top_spot_car_status_t car;
    if (top_spot_car_status_find_entry(entry, &car)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Entry %s is %s. Source: %s",
                 car.entry_number, display_status(car.status),
                 car.judged_source[0] != '\0' ? car.judged_source : "Home Base");
        block_entry(display_status(car.status), msg);
    } else {
        char msg[160];
        snprintf(msg, sizeof(msg), "Entry %s is not in the last synced Home Base roster.", entry);
        block_entry("Entry Not Found", msg);
    }
}

static void show_status_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    add_header(page, "Show Status", s_ui.state.show.show_name,
               top_spot_network_homebase_text());

    int total = top_spot_car_status_count();
    int judged = top_spot_car_status_count_by_state(TOP_SPOT_CAR_STATUS_JUDGED) +
                 top_spot_car_status_count_by_state(TOP_SPOT_CAR_STATUS_CONFLICT);
    int pending = top_spot_record_store_pending_count();
    int remaining = total - judged;
    if (remaining < 0) {
        remaining = 0;
    }

    lv_obj_t *card = ts_card(page);
    lv_obj_set_width(card, 620);
    char value[48];
    snprintf(value, sizeof(value), "%d", total);
    add_value_row(card, "Total Cars", value);
    snprintf(value, sizeof(value), "%d", judged);
    add_value_row(card, "Judged", value);
    snprintf(value, sizeof(value), "%d", remaining);
    add_value_row(card, "Remaining", value);
    snprintf(value, sizeof(value), "%d", pending);
    add_value_row(card, "Pending Sync", value);
    snprintf(value, sizeof(value), "%d", s_ui.state.car_cache.data_revision);
    add_value_row(card, "Data Revision", value);

    lv_obj_t *hint = ts_label(page,
                              total > 0 ? "Showing last synced Home Base roster" : "Waiting for Home Base roster",
                              &lv_font_montserrat_16,
                              total > 0 ? TS_COLOR_GREEN : TS_COLOR_MUTED);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(hint, LV_PCT(100));

    lv_obj_t *row = make_row(page);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *cars = ts_button(row, "View Cars", 220, true);
    lv_obj_add_event_cb(cars, on_cars, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home = ts_button(row, "Home", 180, false);
    lv_obj_add_event_cb(home, on_go_home, LV_EVENT_CLICKED, NULL);
}

static void add_filter_button(lv_obj_t *row, const char *text, top_spot_car_filter_t filter)
{
    lv_obj_t *button = ts_button(row, text, 150, s_ui.cars_filter == filter);
    lv_obj_set_height(button, 44);
    lv_obj_add_event_cb(button, on_cars_filter, LV_EVENT_CLICKED, (void *)(intptr_t)filter);
}

static void show_cars_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    lv_obj_set_style_pad_row(page, 4, LV_PART_MAIN);

    lv_obj_t *header = make_row(page);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *home = ts_button(header, "Home", 140, false);
    lv_obj_set_size(home, 120, 44);
    lv_obj_add_event_cb(home, on_go_home, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title_box = lv_obj_create(header);
    lv_obj_remove_style_all(title_box);
    lv_obj_set_size(title_box, 760, 44);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(title_box, 1, LV_PART_MAIN);
    lv_obj_clear_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);
    ts_label(title_box, "Cars", &lv_font_montserrat_24, TS_COLOR_TEXT);
    lv_obj_t *subtitle = ts_label(title_box, s_ui.state.show.show_name, &lv_font_montserrat_16, TS_COLOR_CYAN);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(subtitle, 740);

    lv_obj_t *filters = make_row(page);
    lv_obj_set_flex_align(filters, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_filter_button(filters, "All", TOP_SPOT_CAR_FILTER_ALL);
    add_filter_button(filters, "Unjudged", TOP_SPOT_CAR_FILTER_UNJUDGED);
    add_filter_button(filters, "Judged", TOP_SPOT_CAR_FILTER_JUDGED);
    add_filter_button(filters, "Pending", TOP_SPOT_CAR_FILTER_PENDING);

    lv_obj_t *lookup_row = make_row(page);
    lv_obj_set_flex_align(lookup_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_ui.cars_lookup_field = add_field(lookup_row, "Entry Lookup", "", 7, true);
    lv_obj_set_size(lv_obj_get_parent(s_ui.cars_lookup_field), 424, 64);
    lv_obj_set_height(s_ui.cars_lookup_field, 38);
    lv_obj_t *lookup = ts_button(lookup_row, "Lookup", 160, true);
    lv_obj_set_height(lookup, 44);
    lv_obj_add_event_cb(lookup, on_lookup, LV_EVENT_CLICKED, NULL);

    lv_obj_t *list = lv_obj_create(page);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100),
                    (TS_CARS_ROW_HEIGHT * TS_CARS_ROWS_PER_PAGE) +
                    (TS_CARS_ROW_GAP * (TS_CARS_ROWS_PER_PAGE - 1)));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, TS_CARS_ROW_GAP, LV_PART_MAIN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    int visible = top_spot_car_status_visible_count(s_ui.cars_filter);
    int page_count = visible > 0 ? ((visible + TS_CARS_ROWS_PER_PAGE - 1) / TS_CARS_ROWS_PER_PAGE) : 1;
    if (s_ui.cars_page >= page_count) {
        s_ui.cars_page = page_count - 1;
    }
    if (s_ui.cars_page < 0) {
        s_ui.cars_page = 0;
    }
    int visible_start = s_ui.cars_page * TS_CARS_ROWS_PER_PAGE;
    ESP_LOGI(TAG, "CARS UI: filter=%s matches=%d visible_start=%d row_pool=%d",
             cars_filter_name(s_ui.cars_filter), visible, visible_start, TS_CARS_ROWS_PER_PAGE);

    for (int row_index = 0; row_index < TS_CARS_ROWS_PER_PAGE; row_index++) {
        int visible_index = visible_start + row_index;
        if (visible_index >= visible) {
            break;
        }
        top_spot_car_status_t car;
        if (!top_spot_car_status_get_visible(s_ui.cars_filter, visible_index, &car)) {
            break;
        }
        lv_obj_t *row = ts_card(list);
        s_ui.cars_rows[row_index] = row;
        lv_obj_set_size(row, LV_PCT(100), TS_CARS_ROW_HEIGHT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(row, 5, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(row, 5, LV_PART_MAIN);
        lv_obj_set_style_pad_left(row, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_right(row, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);

        char title[128];
        snprintf(title, sizeof(title), "#%s  %s %s %s",
                 car.entry_number, car.year, car.make, car.model);
        lv_obj_t *label = ts_label(row, title, &lv_font_montserrat_16, TS_COLOR_TEXT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 650);

        ts_label(row, display_status(car.status), &lv_font_montserrat_16, status_color(car.status));
    }

    lv_obj_t *nav = make_row(page);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *prev = ts_button(nav, "Prev", 130, false);
    lv_obj_set_height(prev, 44);
    lv_obj_add_event_cb(prev, on_cars_prev, LV_EVENT_CLICKED, NULL);
    if (s_ui.cars_page == 0) {
        lv_obj_add_state(prev, LV_STATE_DISABLED);
    }
    char page_text[48];
    snprintf(page_text, sizeof(page_text), "Page %d of %d", s_ui.cars_page + 1, page_count);
    s_ui.cars_page_label = ts_label(nav, page_text, &lv_font_montserrat_16, TS_COLOR_MUTED);
    lv_obj_set_width(s_ui.cars_page_label, 220);
    lv_obj_set_style_text_align(s_ui.cars_page_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_t *next = ts_button(nav, "Next", 130, true);
    lv_obj_set_height(next, 44);
    lv_obj_add_event_cb(next, on_cars_next, LV_EVENT_CLICKED, NULL);
    if (s_ui.cars_page >= page_count - 1) {
        lv_obj_add_state(next, LV_STATE_DISABLED);
    }

    s_ui.keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(s_ui.keyboard, BSP_LCD_H_RES, 220);
    lv_obj_align(s_ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_popovers(s_ui.keyboard, false);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_ui.keyboard, on_keyboard_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ui.keyboard, on_keyboard_ready, LV_EVENT_CANCEL, NULL);
}

static void show_entry_blocked(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ts_label(page, s_ui.blocked_title, &lv_font_montserrat_32, TS_COLOR_ACCENT);
    lv_obj_t *message = ts_label(page, s_ui.blocked_message, &lv_font_montserrat_20, TS_COLOR_TEXT);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message, 720);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *row = make_row(page);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *back = ts_button(row, "Back", 180, false);
    lv_obj_add_event_cb(back, on_details, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cars = ts_button(row, "View Cars", 220, true);
    lv_obj_add_event_cb(cars, on_cars, LV_EVENT_CLICKED, NULL);
}

static void keyboard_hide(void)
{
    if (s_ui.keyboard != NULL) {
        lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_ui.keyboard, NULL);
    }
}

static void on_keyboard_ready(lv_event_t *event)
{
    (void)event;
    keyboard_hide();
}

static void on_field_focused(lv_event_t *event)
{
    lv_obj_t *ta = lv_event_get_target_obj(event);
    intptr_t numeric = (intptr_t)lv_event_get_user_data(event);

    if (s_ui.keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(s_ui.keyboard, ta);
    lv_keyboard_set_mode(s_ui.keyboard, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}

static lv_obj_t *add_field(lv_obj_t *parent, const char *label, const char *value, int max_len, bool numeric)
{
    lv_obj_t *group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, 424, 82);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(group, 7, LV_PART_MAIN);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    ts_label(group, label, &lv_font_montserrat_16, TS_COLOR_MUTED);

    lv_obj_t *ta = lv_textarea_create(group);
    lv_obj_set_size(ta, LV_PCT(100), 48);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_text(ta, value);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, ts_color(TS_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, ts_color(TS_COLOR_SURFACE_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, ts_color(TS_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, ts_color(TS_COLOR_CYAN), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ta, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ta, 14, LV_PART_MAIN);
    lv_obj_add_event_cb(ta, on_field_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)numeric);
    return ta;
}

static void save_details_from_fields(void)
{
    top_spot_record_t *car = &s_ui.state.current;
    set_text(car->entry_number, sizeof(car->entry_number), s_ui.detail_fields[0]);
    set_text(car->participant, sizeof(car->participant), s_ui.detail_fields[1]);
    set_text(car->year, sizeof(car->year), s_ui.detail_fields[2]);
    set_text(car->make, sizeof(car->make), s_ui.detail_fields[3]);
    set_text(car->model, sizeof(car->model), s_ui.detail_fields[4]);
}

static void on_details_continue(lv_event_t *event)
{
    (void)event;
    save_details_from_fields();
    keyboard_hide();
    if (!check_entry_can_be_judged()) {
        return;
    }
    show_screen(TS_SCREEN_JUDGE);
}

static void show_details(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    add_header(page, "Enter Car", "Vehicle Details", "Tap a field to enter the car information.");

    lv_obj_t *card = ts_card(page);
    lv_obj_set_size(card, LV_PCT(100), 270);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(card, 26, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    top_spot_record_t *car = &s_ui.state.current;
    s_ui.detail_fields[0] = add_field(card, "Entry Number", car->entry_number, 7, true);
    s_ui.detail_fields[1] = add_field(card, "Participant", car->participant, 79, false);
    s_ui.detail_fields[2] = add_field(card, "Year", car->year, 7, true);
    s_ui.detail_fields[3] = add_field(card, "Make", car->make, 47, false);
    s_ui.detail_fields[4] = add_field(card, "Model", car->model, 47, false);

    add_nav(page, "Home", on_go_home, "Continue", on_details_continue);

    s_ui.keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(s_ui.keyboard, BSP_LCD_H_RES, 230);
    lv_obj_align(s_ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_popovers(s_ui.keyboard, false);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_ui.keyboard, on_keyboard_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ui.keyboard, on_keyboard_ready, LV_EVENT_CANCEL, NULL);
}

static void update_score_buttons(void)
{
    int selected = s_ui.state.current.scores[s_ui.state.category_index];
    int score_range_max = s_ui.state.current.score_range_max > 0
                              ? s_ui.state.current.score_range_max
                              : TOP_SPOT_DEFAULT_SCORE_MAX;
    if (score_range_max > TOP_SPOT_MAX_SCORE_RANGE) {
        score_range_max = TOP_SPOT_MAX_SCORE_RANGE;
    }
    for (int i = 0; i < score_range_max; i++) {
        bool checked = selected == i + 1;
        ts_set_button_checked(s_ui.score_buttons[i], checked);
        lv_obj_t *label = lv_obj_get_child(s_ui.score_buttons[i], 0);
        if (label != NULL) {
            lv_obj_set_style_text_color(label, ts_color(checked ? 0x111827 : TS_COLOR_TEXT), LV_PART_MAIN);
        }
    }
}

static void on_score(lv_event_t *event)
{
    intptr_t value = (intptr_t)lv_event_get_user_data(event);
    s_ui.state.current.scores[s_ui.state.category_index] = (int)value;
    lv_label_set_text(s_ui.score_hint, "Score saved");
    update_score_buttons();
}

static void on_judge_back(lv_event_t *event)
{
    (void)event;
    if (s_ui.state.category_index > 0) {
        s_ui.state.category_index--;
        show_screen(TS_SCREEN_JUDGE);
    } else {
        show_screen(TS_SCREEN_DETAILS);
    }
}

static void on_judge_next(lv_event_t *event)
{
    (void)event;
    int score_range_max = s_ui.state.current.score_range_max > 0
                              ? s_ui.state.current.score_range_max
                              : TOP_SPOT_DEFAULT_SCORE_MAX;
    int score = s_ui.state.current.scores[s_ui.state.category_index];
    if (score < 1 || score > score_range_max) {
        char hint[72];
        snprintf(hint, sizeof(hint), "Choose a score from 1 to %d before continuing.", score_range_max);
        lv_label_set_text(s_ui.score_hint, hint);
        return;
    }

    if (s_ui.state.category_index < s_ui.state.current.score_count - 1) {
        s_ui.state.category_index++;
        show_screen(TS_SCREEN_JUDGE);
    } else {
        show_screen(TS_SCREEN_AWARDS);
    }
}

static void show_judge(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    if (s_ui.state.current.score_count == 0) {
        add_header(page, "No Show Loaded", "Show Configuration Required",
                   "Connect to Home Base to download the active show before judging.");
        add_nav(page, "Home", on_go_home, NULL, NULL);
        return;
    }
    const char *category_name = s_ui.state.current.score_category_names[s_ui.state.category_index];

    char progress[48];
    snprintf(progress, sizeof(progress), "Category %d of %d",
             s_ui.state.category_index + 1, s_ui.state.current.score_count);
    add_header(page, progress, category_name, "Select one score. Scores are preserved when you go back.");

    lv_obj_t *card = ts_card(page);
    lv_obj_set_size(card, LV_PCT(100), 278);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int score_range_max = s_ui.state.current.score_range_max > 0
                              ? s_ui.state.current.score_range_max
                              : TOP_SPOT_DEFAULT_SCORE_MAX;
    if (score_range_max > TOP_SPOT_MAX_SCORE_RANGE) {
        score_range_max = TOP_SPOT_MAX_SCORE_RANGE;
    }

    lv_obj_t *grid = lv_obj_create(card);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 12, LV_PART_MAIN);

    int button_width = score_range_max > 5 ? 94 : 110;
    for (int i = 0; i < score_range_max; i++) {
        char text[4];
        snprintf(text, sizeof(text), "%d", i + 1);
        s_ui.score_buttons[i] = ts_button(grid, text, button_width, false);
        lv_obj_set_height(s_ui.score_buttons[i], score_range_max > 5 ? 72 : 86);
        lv_obj_add_flag(s_ui.score_buttons[i], LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(s_ui.score_buttons[i], ts_color(TS_COLOR_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_border_color(s_ui.score_buttons[i], ts_color(TS_COLOR_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(s_ui.score_buttons[i], 12, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_color(s_ui.score_buttons[i], ts_color(TS_COLOR_ACCENT_DARK), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_opa(s_ui.score_buttons[i], LV_OPA_30, LV_STATE_CHECKED);
        lv_obj_add_event_cb(s_ui.score_buttons[i], on_score, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
    }

    char hint[64];
    snprintf(hint, sizeof(hint), "Scores are 1 low to %d high.", score_range_max);
    s_ui.score_hint = ts_label(card, hint, &lv_font_montserrat_16, TS_COLOR_MUTED);
    lv_obj_set_style_text_align(s_ui.score_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(s_ui.score_hint, LV_PCT(100));
    update_score_buttons();

    add_nav(page, "Back", on_judge_back,
            s_ui.state.category_index == s_ui.state.current.score_count - 1 ? "Awards" : "Next", on_judge_next);
}

static void on_award_toggle(lv_event_t *event)
{
    intptr_t index = (intptr_t)lv_event_get_user_data(event);
    bool checked = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    s_ui.state.current.nominations[index] = checked;
}

static void on_awards_back(lv_event_t *event)
{
    (void)event;
    s_ui.state.category_index = s_ui.state.current.score_count > 0 ? s_ui.state.current.score_count - 1 : 0;
    show_screen(TS_SCREEN_JUDGE);
}

static void on_awards_continue(lv_event_t *event)
{
    (void)event;
    show_screen(TS_SCREEN_PHOTOS);
}

static void show_awards(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    add_header(page, "Award Nominations", "Judge-Selected Awards",
               "Tap any awards this car should be considered for. None are required.");

    lv_obj_t *grid = ts_card(page);
    lv_obj_set_size(grid, LV_PCT(100), 300);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 14, LV_PART_MAIN);

    for (int i = 0; i < s_ui.state.current.award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        lv_obj_t *button = ts_button(grid, s_ui.state.current.award_names[i], 206, false);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(button, ts_color(TS_COLOR_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_border_color(button, ts_color(TS_COLOR_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(button, 10, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_color(button, ts_color(TS_COLOR_ACCENT_DARK), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_opa(button, LV_OPA_20, LV_STATE_CHECKED);
        ts_set_button_checked(button, s_ui.state.current.nominations[i]);
        lv_obj_add_event_cb(button, on_award_toggle, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    }

    add_nav(page, "Back", on_awards_back, "Photos", on_awards_continue);
}

static bool photo_is_captured(top_spot_photo_type_t type)
{
    return type == TOP_SPOT_PHOTO_VEHICLE
               ? s_ui.state.current.vehicle_photo_captured
               : s_ui.state.current.judge_sheet_photo_captured;
}

static const char *photo_title(top_spot_photo_type_t type)
{
    return type == TOP_SPOT_PHOTO_VEHICLE ? "Vehicle Photo" : "Judge Sheet Photo";
}

static void camera_result_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui.camera_result_queue == NULL) {
        return;
    }

    ts_camera_result_t result;
    while (xQueueReceive(s_ui.camera_result_queue, &result, 0) == pdTRUE) {
        s_ui.camera_busy = false;

        if (result.action == TS_CAMERA_ACTION_CLOSE) {
            ESP_LOGI(TAG, "Camera close complete; Photos screen resumed");
            show_screen(TS_SCREEN_PHOTOS);
            continue;
        }

        ESP_LOGI(TAG, "Camera capture UI completion received: result=%s size=%u",
                 esp_err_to_name(result.result), (unsigned)result.file_size);
        if (result.result == ESP_OK) {
            top_spot_record_t *car = &s_ui.state.current;
            char *dest = result.photo_type == TOP_SPOT_PHOTO_VEHICLE
                             ? car->vehicle_photo_path
                             : car->judge_sheet_photo_path;
            size_t path_len = strnlen(result.path, sizeof(result.path) - 1);
            memcpy(dest, result.path, path_len);
            dest[path_len] = '\0';
            if (result.photo_type == TOP_SPOT_PHOTO_VEHICLE) {
                car->vehicle_photo_captured = true;
            } else {
                car->judge_sheet_photo_captured = true;
            }
            ESP_LOGI(TAG, "Photo captured; Photos screen resumed");
            show_screen(TS_SCREEN_PHOTOS);
        } else if (s_ui.screen == TS_SCREEN_CAMERA && s_ui.camera_status != NULL) {
            if (result.result == ESP_ERR_INVALID_STATE) {
                lv_label_set_text(s_ui.camera_status, "Camera is getting ready. Try again.");
            } else {
                char msg[96];
                snprintf(msg, sizeof(msg), "Capture failed: %s", esp_err_to_name(result.result));
                lv_label_set_text(s_ui.camera_status, msg);
            }
            if (s_ui.camera_back_button != NULL) {
                lv_obj_clear_state(s_ui.camera_back_button, LV_STATE_DISABLED);
            }
            if (s_ui.camera_capture_button != NULL) {
                lv_obj_clear_state(s_ui.camera_capture_button, LV_STATE_DISABLED);
            }
        }
    }
}

static void camera_worker_task(void *arg)
{
    ts_camera_job_t *job = (ts_camera_job_t *)arg;
    ts_camera_result_t result = {
        .action = job->action,
        .photo_type = job->photo_type,
        .result = ESP_OK,
    };

    if (job->action == TS_CAMERA_ACTION_CAPTURE) {
        ESP_LOGI(TAG, "Camera capture worker started for %s", photo_title(job->photo_type));
        result.result = top_spot_camera_capture_jpeg(job->photo_type,
                                                     job->entry_number,
                                                     result.path,
                                                     sizeof(result.path),
                                                     &result.file_size);
        ESP_LOGI(TAG, "Camera capture complete: %s", esp_err_to_name(result.result));
    } else {
        ESP_LOGI(TAG, "Camera close worker started");
    }

    esp_err_t stop_err = top_spot_camera_stop_preview();
    ESP_LOGI(TAG, "Camera preview stopped after %s: %s",
             job->action == TS_CAMERA_ACTION_CAPTURE ? "capture" : "close",
             esp_err_to_name(stop_err));
    if (result.result == ESP_OK) {
        result.result = stop_err;
    }

    if (s_ui.camera_result_queue != NULL) {
        if (xQueueSend(s_ui.camera_result_queue, &result, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "Camera UI completion posted");
        } else {
            ESP_LOGW(TAG, "Camera UI completion queue full");
        }
    }

    free(job);
    vTaskDelete(NULL);
}

static esp_err_t start_camera_job(ts_camera_action_t action)
{
    if (s_ui.camera_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    ts_camera_job_t *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return ESP_ERR_NO_MEM;
    }

    job->action = action;
    job->photo_type = s_ui.active_photo_type;
    snprintf(job->entry_number, sizeof(job->entry_number), "%s", s_ui.state.current.entry_number);
    s_ui.camera_busy = true;

    BaseType_t task_ok = xTaskCreatePinnedToCore(camera_worker_task,
                                                 "camera_worker",
                                                 8 * 1024,
                                                 job,
                                                 3,
                                                 NULL,
                                                 0);
    if (task_ok != pdPASS) {
        s_ui.camera_busy = false;
        free(job);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void on_photo_card(lv_event_t *event)
{
    s_ui.active_photo_type = (top_spot_photo_type_t)(intptr_t)lv_event_get_user_data(event);
    show_screen(TS_SCREEN_CAMERA);
}

static void add_photo_card(lv_obj_t *parent, top_spot_photo_type_t type)
{
    lv_obj_t *card = ts_card(parent);
    lv_obj_set_size(card, 430, 210);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_photo_card, LV_EVENT_CLICKED, (void *)(intptr_t)type);
    ts_label(card, photo_title(type), &lv_font_montserrat_24, TS_COLOR_TEXT);

    lv_obj_t *box = lv_obj_create(card);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_PCT(100), 104);
    lv_obj_set_style_bg_color(box, ts_color(TS_COLOR_SURFACE_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, ts_color(TS_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(box, 16, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    const bool captured = photo_is_captured(type);
    lv_obj_t *label = ts_label(box, captured ? "Photo Captured" : "Tap to capture",
                               &lv_font_montserrat_20, captured ? TS_COLOR_GREEN : TS_COLOR_MUTED);
    lv_obj_center(label);

    lv_obj_t *button = ts_button(card, captured ? "Retake" : "Capture", 180, !captured);
    lv_obj_add_event_cb(button, on_photo_card, LV_EVENT_CLICKED, (void *)(intptr_t)type);
}

static void on_photos_back(lv_event_t *event)
{
    (void)event;
    show_screen(TS_SCREEN_AWARDS);
}

static void on_photos_continue(lv_event_t *event)
{
    (void)event;
    show_screen(TS_SCREEN_REVIEW);
}

static void show_photos(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    add_header(page, "Photos", "Capture Required Photos", "Capture a vehicle photo and judge sheet photo before saving.");

    lv_obj_t *row = make_row(page);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_photo_card(row, TOP_SPOT_PHOTO_VEHICLE);
    add_photo_card(row, TOP_SPOT_PHOTO_JUDGE_SHEET);

    add_nav(page, "Back", on_photos_back, "Review", on_photos_continue);
}

static void on_camera_back(lv_event_t *event)
{
    (void)event;
    if (s_ui.camera_busy) {
        return;
    }
    if (s_ui.camera_status != NULL) {
        lv_label_set_text(s_ui.camera_status, "Closing camera...");
    }
    if (s_ui.camera_back_button != NULL) {
        lv_obj_add_state(s_ui.camera_back_button, LV_STATE_DISABLED);
    }
    if (s_ui.camera_capture_button != NULL) {
        lv_obj_add_state(s_ui.camera_capture_button, LV_STATE_DISABLED);
    }
    esp_err_t err = start_camera_job(TS_CAMERA_ACTION_CLOSE);
    if (err != ESP_OK && s_ui.camera_status != NULL) {
        s_ui.camera_busy = false;
        lv_label_set_text(s_ui.camera_status, "Camera close failed");
    }
}

static void on_camera_capture(lv_event_t *event)
{
    (void)event;
    if (s_ui.camera_busy) {
        return;
    }
    if (!top_spot_storage_is_ready()) {
        lv_label_set_text(s_ui.camera_status, "Storage Problem");
        return;
    }
    if (!top_spot_camera_is_ready()) {
        lv_label_set_text(s_ui.camera_status, "Camera Problem");
        return;
    }

    lv_label_set_text(s_ui.camera_status, "Saving photo...");
    if (s_ui.camera_back_button != NULL) {
        lv_obj_add_state(s_ui.camera_back_button, LV_STATE_DISABLED);
    }
    if (s_ui.camera_capture_button != NULL) {
        lv_obj_add_state(s_ui.camera_capture_button, LV_STATE_DISABLED);
    }
    ESP_LOGI(TAG, "Camera capture requested; worker task starting");
    esp_err_t err = start_camera_job(TS_CAMERA_ACTION_CAPTURE);
    if (err != ESP_OK) {
        s_ui.camera_busy = false;
        char msg[96];
        snprintf(msg, sizeof(msg), "Capture failed: %s", esp_err_to_name(err));
        lv_label_set_text(s_ui.camera_status, msg);
        if (s_ui.camera_back_button != NULL) {
            lv_obj_clear_state(s_ui.camera_back_button, LV_STATE_DISABLED);
        }
        if (s_ui.camera_capture_button != NULL) {
            lv_obj_clear_state(s_ui.camera_capture_button, LV_STATE_DISABLED);
        }
    }
}

static void show_camera(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *canvas = lv_canvas_create(screen);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_size(canvas, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(canvas);
    lv_obj_set_style_bg_color(canvas, ts_color(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *top = lv_obj_create(screen);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, BSP_LCD_H_RES, 82);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top, ts_color(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_left(top, 40, LV_PART_MAIN);
    lv_obj_set_style_pad_right(top, 40, LV_PART_MAIN);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ts_label(top, photo_title(s_ui.active_photo_type), &lv_font_montserrat_24, TS_COLOR_TEXT);
    s_ui.camera_status = ts_label(top, "Starting camera...", &lv_font_montserrat_20, TS_COLOR_CYAN);
    s_ui.wifi_indicator = ts_label(top, "---- Wi-Fi disconnected", &lv_font_montserrat_16, TS_COLOR_MUTED);
    add_battery_indicator(top);
    /* Use the spare lower edge of the camera header without shrinking its row. */
    lv_obj_add_flag(s_ui.battery_indicator, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_ui.battery_indicator, LV_ALIGN_BOTTOM_RIGHT, 0, -2);

    lv_obj_t *bottom = lv_obj_create(screen);
    lv_obj_remove_style_all(bottom);
    lv_obj_set_size(bottom, BSP_LCD_H_RES, 110);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom, ts_color(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_left(bottom, 40, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bottom, 40, LV_PART_MAIN);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bottom, 18, LV_PART_MAIN);

    s_ui.camera_back_button = ts_button(bottom, "Back", 180, false);
    lv_obj_add_event_cb(s_ui.camera_back_button, on_camera_back, LV_EVENT_CLICKED, NULL);
    s_ui.camera_capture_button = ts_button(bottom, "Capture", 240, true);
    lv_obj_add_event_cb(s_ui.camera_capture_button, on_camera_capture, LV_EVENT_CLICKED, NULL);

    esp_err_t err = top_spot_camera_start_preview(canvas);
    if (err == ESP_OK) {
        lv_label_set_text(s_ui.camera_status, "Camera Ready");
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "Camera Problem: %s", esp_err_to_name(err));
        lv_label_set_text(s_ui.camera_status, msg);
    }
}

static void add_score_rows(lv_obj_t *card)
{
    int score_range_max = s_ui.state.current.score_range_max > 0
                              ? s_ui.state.current.score_range_max
                              : TOP_SPOT_DEFAULT_SCORE_MAX;
    for (int i = 0; i < s_ui.state.current.score_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        char score[12];
        snprintf(score, sizeof(score), "%d / %d", s_ui.state.current.scores[i], score_range_max);
        add_value_row(card, s_ui.state.current.score_category_names[i], score);
    }

    char total[24];
    snprintf(total, sizeof(total), "%d / %d", top_spot_total_score(&s_ui.state.current),
             top_spot_record_max_score(&s_ui.state.current));
    add_value_row(card, "Total", total);
}

static void add_nomination_rows(lv_obj_t *card)
{
    if (top_spot_selected_nomination_count(&s_ui.state.current) == 0) {
        ts_label(card, "None selected", &lv_font_montserrat_16, TS_COLOR_MUTED);
        return;
    }

    for (int i = 0; i < s_ui.state.current.award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        if (s_ui.state.current.nominations[i]) {
            ts_label(card, s_ui.state.current.award_names[i], &lv_font_montserrat_16, TS_COLOR_TEXT);
        }
    }
}

static void on_review_back(lv_event_t *event)
{
    (void)event;
    show_screen(TS_SCREEN_PHOTOS);
}

static void on_review_submit(lv_event_t *event)
{
    (void)event;
    if (!s_ui.state.current.vehicle_photo_captured || !s_ui.state.current.judge_sheet_photo_captured) {
        return;
    }
    int score_range_max = s_ui.state.current.score_range_max > 0
                              ? s_ui.state.current.score_range_max
                              : TOP_SPOT_DEFAULT_SCORE_MAX;
    for (int i = 0; i < s_ui.state.current.score_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        int score = s_ui.state.current.scores[i];
        if (score < 1 || score > score_range_max) {
            ESP_LOGW(TAG, "Submit blocked: %s score=%d outside snapshot range 1-%d",
                     s_ui.state.current.score_category_names[i], score, score_range_max);
            show_screen(TS_SCREEN_JUDGE);
            return;
        }
    }
    esp_err_t err = top_spot_record_store_save(&s_ui.state.current, &s_ui.state);
    if (err == ESP_OK) {
        top_spot_car_status_overlay_pending();
        show_screen(TS_SCREEN_SAVED);
    } else {
        ESP_LOGE(TAG, "Failed to save completed record: %s", esp_err_to_name(err));
    }
}

static void show_review(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    lv_obj_set_style_pad_row(page, 8, LV_PART_MAIN);
    add_header(page, "Review", "Confirm Local Record", "Review the entry before saving this car locally.");

    lv_obj_t *content = lv_obj_create(page);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), 280);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 14, LV_PART_MAIN);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *details = ts_card(content);
    lv_obj_set_size(details, 456, LV_SIZE_CONTENT);
    ts_label(details, "Vehicle", &lv_font_montserrat_24, TS_COLOR_TEXT);
    add_value_row(details, "Entry Number", s_ui.state.current.entry_number);
    add_value_row(details, "Participant", s_ui.state.current.participant);
    add_value_row(details, "Year", s_ui.state.current.year);
    add_value_row(details, "Make", s_ui.state.current.make);
    add_value_row(details, "Model", s_ui.state.current.model);

    lv_obj_t *scores = ts_card(content);
    lv_obj_set_size(scores, 456, LV_SIZE_CONTENT);
    ts_label(scores, "Scores", &lv_font_montserrat_24, TS_COLOR_TEXT);
    add_score_rows(scores);

    lv_obj_t *awards = ts_card(content);
    lv_obj_set_size(awards, 456, LV_SIZE_CONTENT);
    ts_label(awards, "Award Nominations", &lv_font_montserrat_24, TS_COLOR_TEXT);
    add_nomination_rows(awards);

    lv_obj_t *photos = ts_card(content);
    lv_obj_set_size(photos, 456, LV_SIZE_CONTENT);
    ts_label(photos, "Photos", &lv_font_montserrat_24, TS_COLOR_TEXT);
    add_value_row(photos, "Vehicle Photo", s_ui.state.current.vehicle_photo_captured ? "Captured" : "Missing");
    add_value_row(photos, "Judge Sheet Photo", s_ui.state.current.judge_sheet_photo_captured ? "Captured" : "Missing");

    if (!s_ui.state.current.vehicle_photo_captured || !s_ui.state.current.judge_sheet_photo_captured) {
        lv_obj_t *warning = ts_label(page, "Capture both required photos before saving this car.",
                                     &lv_font_montserrat_16, TS_COLOR_ACCENT);
        lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_width(warning, LV_PCT(100));
    }

    add_nav(page, "Back", on_review_back, "Save Locally", on_review_submit);
}

static void on_next_car(lv_event_t *event)
{
    (void)event;
    top_spot_start_new_car(&s_ui.state);
    show_screen(TS_SCREEN_DETAILS);
}

static void show_saved(void)
{
    lv_obj_t *screen = lv_screen_active();
    ts_style_screen(screen);

    lv_obj_t *page = make_page(screen);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ts_label(page, "Saved Locally", &lv_font_montserrat_32, TS_COLOR_GREEN);
    lv_obj_t *msg = ts_label(page, "This judging record is stored on the microSD card.",
                             &lv_font_montserrat_20, TS_COLOR_MUTED);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(msg, 620);

    char count[48];
    snprintf(count, sizeof(count), "%d local record%s saved", s_ui.state.saved_count,
             s_ui.state.saved_count == 1 ? "" : "s");
    lv_obj_t *count_label = ts_label(page, count, &lv_font_montserrat_24, TS_COLOR_TEXT);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(count_label, LV_PCT(100));

    lv_obj_t *row = make_row(page);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *next = ts_button(row, "Next Car", 220, true);
    lv_obj_add_event_cb(next, on_next_car, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home = ts_button(row, "Home", 180, false);
    lv_obj_add_event_cb(home, on_go_home, LV_EVENT_CLICKED, NULL);
}

static void show_screen(ts_screen_id_t screen)
{
    if (s_ui.screen == TS_SCREEN_CAMERA && screen != TS_SCREEN_CAMERA) {
        top_spot_camera_stop_preview();
    }

    s_ui.screen = screen;
    s_ui.keyboard = NULL;
    memset(s_ui.detail_fields, 0, sizeof(s_ui.detail_fields));
    memset(s_ui.score_buttons, 0, sizeof(s_ui.score_buttons));
    s_ui.score_hint = NULL;
    s_ui.camera_status = NULL;
    s_ui.camera_back_button = NULL;
    s_ui.camera_capture_button = NULL;
    s_ui.wifi_indicator = NULL;
    s_ui.battery_indicator = NULL;
    s_ui.homebase_indicator = NULL;
    s_ui.home_wifi_value = NULL;
    s_ui.home_homebase_value = NULL;
    s_ui.home_storage_value = NULL;
    s_ui.home_show_value = NULL;
    s_ui.home_show_help = NULL;
    s_ui.home_saved_value = NULL;
    s_ui.home_sync_value = NULL;
    s_ui.home_enter_button = NULL;
    s_ui.home_status_button = NULL;
    memset(s_ui.cars_rows, 0, sizeof(s_ui.cars_rows));
    s_ui.cars_page_label = NULL;
    s_ui.cars_lookup_field = NULL;

    switch (screen) {
    case TS_SCREEN_HOME:
        show_home();
        break;
    case TS_SCREEN_SHOW_STATUS:
        show_status_screen();
        break;
    case TS_SCREEN_CARS:
        show_cars_screen();
        break;
    case TS_SCREEN_ENTRY_BLOCKED:
        show_entry_blocked();
        break;
    case TS_SCREEN_DETAILS:
        show_details();
        break;
    case TS_SCREEN_JUDGE:
        show_judge();
        break;
    case TS_SCREEN_AWARDS:
        show_awards();
        break;
    case TS_SCREEN_PHOTOS:
        show_photos();
        break;
    case TS_SCREEN_CAMERA:
        show_camera();
        break;
    case TS_SCREEN_REVIEW:
        show_review();
        break;
    case TS_SCREEN_SAVED:
        show_saved();
        break;
    }
}

void top_spot_ui_prepare_state(void)
{
    if (s_ui.state_prepared) {
        ESP_LOGI(TAG, "BOOT TRACE: ui_prepare already complete saved=%d cars=%d",
                 s_ui.state.saved_count, s_ui.state.car_cache.car_count);
        return;
    }

    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare entry");
    top_spot_state_init(&s_ui.state);
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare state initialized");
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare before show store init");
    esp_err_t show_err = top_spot_show_store_init(&s_ui.state);
    if (show_err != ESP_OK) {
        ESP_LOGW(TAG, "No active show cache available at startup: %s", esp_err_to_name(show_err));
    }
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare after show store init err=%s", esp_err_to_name(show_err));
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare before record store init");
    esp_err_t record_err = top_spot_record_store_init(&s_ui.state);
    if (record_err != ESP_OK) {
        ESP_LOGW(TAG, "Persisted records unavailable at startup: %s", esp_err_to_name(record_err));
    }
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare after record store init err=%s saved=%d",
             esp_err_to_name(record_err), s_ui.state.saved_count);
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare before car status store init");
    esp_err_t car_cache_err = top_spot_car_status_store_init(&s_ui.state);
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare after car status store init err=%s cars=%d",
             esp_err_to_name(car_cache_err), s_ui.state.car_cache.car_count);

    s_ui.state_prepared = true;
    ESP_LOGI(TAG, "BOOT TRACE: ui_prepare complete");
}

void top_spot_ui_start(void)
{
    ESP_LOGI(TAG, "BOOT TRACE: ui_start entry");
    if (!s_ui.state_prepared) {
        ESP_LOGW(TAG, "BOOT TRACE: ui_start preparing state late");
        top_spot_ui_prepare_state();
    }
    ESP_LOGI(TAG, "BOOT TRACE: ui_start before camera queue/timers");
    s_ui.camera_result_queue = xQueueCreate(3, sizeof(ts_camera_result_t));
    if (s_ui.camera_result_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create camera result queue");
    } else {
        s_ui.camera_result_timer = lv_timer_create(camera_result_timer_cb, 100, NULL);
    }
    s_ui.network_status_timer = lv_timer_create(network_status_timer_cb, 1000, NULL);
    s_ui.battery_status_timer = lv_timer_create(battery_status_timer_cb, 1000, NULL);
    ESP_LOGI(TAG, "BOOT TRACE: ui_start before network attach");
    top_spot_network_attach_state(&s_ui.state);
    ESP_LOGI(TAG, "BOOT TRACE: ui_start before home screen build");
    show_screen(TS_SCREEN_HOME);
    ESP_LOGI(TAG, "BOOT TRACE: ui_start home screen built");
}
