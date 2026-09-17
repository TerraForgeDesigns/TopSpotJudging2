#pragma once

#include "battery_model.h"
#include "esp_err.h"

typedef struct {
    bool valid;
    int adc_mv;       /* Averaged and filtered, calibrated divider voltage. */
    int voltage_mv;   /* Corrected/clamped battery estimate, shared by UI and SOC. */
    int percent;      /* Derived from voltage_mv in the same published snapshot. */
    top_spot_battery_level_t level;
} top_spot_battery_status_t;

/* Call once from app_main, outside the LVGL lock. Failure is non-fatal. */
esp_err_t top_spot_battery_init(void);
/* Copies a cached snapshot only; never performs ADC work or waits for a sample. */
void top_spot_battery_get_status(top_spot_battery_status_t *status);
