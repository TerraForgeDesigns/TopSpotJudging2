#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TOP_SPOT_BATTERY_NORMAL,
    TOP_SPOT_BATTERY_LOW,
    TOP_SPOT_BATTERY_VERY_LOW,
    TOP_SPOT_BATTERY_CRITICAL,
} top_spot_battery_level_t;

typedef struct {
    bool initialized;
    int32_t adc_mv_q8;
} top_spot_battery_filter_t;

int top_spot_battery_filter_mv(top_spot_battery_filter_t *filter, int adc_mv);
int top_spot_battery_voltage_mv(int adc_mv);
int top_spot_battery_correct_voltage_mv(int measured_mv);
int top_spot_battery_percent(int battery_mv);
top_spot_battery_level_t top_spot_battery_level(int percent);
