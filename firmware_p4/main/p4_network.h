#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "judging_model.h"

typedef enum {
    TOP_SPOT_WIFI_DISCONNECTED = 0,
    TOP_SPOT_WIFI_WEAK,
    TOP_SPOT_WIFI_MODERATE,
    TOP_SPOT_WIFI_GOOD,
    TOP_SPOT_WIFI_STRONG,
} top_spot_wifi_level_t;

typedef struct {
    bool wifi_connected;
    bool homebase_connected;
    int rssi_dbm;
    top_spot_wifi_level_t wifi_level;
    char ip_addr[16];
    char homebase_url[96];
    char handheld_id[16];
} top_spot_network_status_t;

esp_err_t top_spot_network_init(void);
void top_spot_network_attach_state(top_spot_app_state_t *state);
void top_spot_network_get_status(top_spot_network_status_t *out_status);
const char *top_spot_network_wifi_text(void);
const char *top_spot_network_homebase_text(void);
const char *top_spot_network_sync_text(void);
