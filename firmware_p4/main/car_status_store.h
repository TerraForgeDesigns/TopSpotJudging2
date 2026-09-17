#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"
#include "judging_model.h"

#define TOP_SPOT_CAR_STATUS_UNJUDGED "unjudged"
#define TOP_SPOT_CAR_STATUS_JUDGED "judged"
#define TOP_SPOT_CAR_STATUS_PENDING_SYNC "pending_sync"
#define TOP_SPOT_CAR_STATUS_CONFLICT "flagged_conflict"

typedef enum {
    TOP_SPOT_CAR_FILTER_ALL,
    TOP_SPOT_CAR_FILTER_UNJUDGED,
    TOP_SPOT_CAR_FILTER_JUDGED,
    TOP_SPOT_CAR_FILTER_PENDING,
} top_spot_car_filter_t;

esp_err_t top_spot_car_status_store_init(top_spot_app_state_t *state);
esp_err_t top_spot_car_status_store_apply_sync(top_spot_app_state_t *state, cJSON *cars, int show_id, int data_revision, bool full_snapshot);
esp_err_t top_spot_car_status_store_save(const top_spot_car_cache_t *cache);
bool top_spot_car_status_store_is_ready(void);
int top_spot_car_status_count(void);
int top_spot_car_status_count_by_state(const char *status);
int top_spot_car_status_visible_count(top_spot_car_filter_t filter);
bool top_spot_car_status_get_visible(top_spot_car_filter_t filter, int visible_index, top_spot_car_status_t *out_car);
bool top_spot_car_status_find_entry(const char *entry, top_spot_car_status_t *out_car);
bool top_spot_car_status_entry_is_pending(const char *entry);
void top_spot_car_status_overlay_pending(void);
