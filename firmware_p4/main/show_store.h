#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "judging_model.h"

#define TOP_SPOT_SHOWS_DIR "/sdcard/topspot/shows"
#define TOP_SPOT_ACTIVE_SHOW_PATH "/sdcard/topspot/shows/active_show.json"

esp_err_t top_spot_show_store_init(top_spot_app_state_t *state);
esp_err_t top_spot_show_store_save_active(const top_spot_show_config_t *show);
esp_err_t top_spot_show_store_load_active(top_spot_show_config_t *show);
bool top_spot_show_store_is_ready(void);
