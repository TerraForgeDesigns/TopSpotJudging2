#include "battery_model.h"

#include <stddef.h>

/* Battery-only measurement fit: mean error 162.667 mV, rounded to 1 mV.
 * Empirical correction after filtering/scaling; no hardware cause is assumed.
 * USB power cannot be detected reliably on this board: even with this
 * correction, USB-connected voltage/SOC does not represent the battery. */
static const int BATTERY_VOLTAGE_OFFSET_MV = 163;
static const int BATTERY_VOLTAGE_MIN_MV = 3000;
static const int BATTERY_VOLTAGE_MAX_MV = 4200;

/* Generic single-cell Li-ion estimate, not a pack-specific fuel gauge.
 * Load, temperature, ageing and charging affect the terminal voltage.
 * Linear interpolation is only between these deliberately nonlinear knots. */
static const struct { int mv; int percent; } s_curve[] = {
    {3300, 0}, {3500, 5}, {3600, 10}, {3700, 20}, {3750, 30},
    {3800, 40}, {3850, 50}, {3900, 60}, {3950, 70}, {4000, 80},
    {4100, 90}, {4200, 100},
};

int top_spot_battery_filter_mv(top_spot_battery_filter_t *filter, int adc_mv)
{
    int32_t target = adc_mv * 256;
    if (!filter->initialized) {
        filter->adc_mv_q8 = target;
        filter->initialized = true;
    } else {
        /* EMA alpha=1/4; fractional state avoids integer-mV dead bands. */
        filter->adc_mv_q8 += (target - filter->adc_mv_q8) / 4;
    }
    return (filter->adc_mv_q8 + 128) / 256;
}

int top_spot_battery_voltage_mv(int adc_mv)
{
    /* R92=200k, R93=100k: BAT = ADC * (R92+R93)/R93. */
    return adc_mv * 3;
}

int top_spot_battery_correct_voltage_mv(int measured_mv)
{
    int64_t corrected_mv = (int64_t)measured_mv + BATTERY_VOLTAGE_OFFSET_MV;
    if (corrected_mv < BATTERY_VOLTAGE_MIN_MV) return BATTERY_VOLTAGE_MIN_MV;
    if (corrected_mv > BATTERY_VOLTAGE_MAX_MV) return BATTERY_VOLTAGE_MAX_MV;
    return (int)corrected_mv;
}

int top_spot_battery_percent(int battery_mv)
{
    if (battery_mv <= s_curve[0].mv) return 0;
    for (size_t i = 1; i < sizeof(s_curve) / sizeof(s_curve[0]); ++i) {
        if (battery_mv < s_curve[i].mv) {
            int span = s_curve[i].mv - s_curve[i - 1].mv;
            return s_curve[i - 1].percent +
                ((battery_mv - s_curve[i - 1].mv) *
                 (s_curve[i].percent - s_curve[i - 1].percent) + span / 2) / span;
        }
    }
    return 100;
}

top_spot_battery_level_t top_spot_battery_level(int percent)
{
    if (percent <= 5) return TOP_SPOT_BATTERY_CRITICAL;
    if (percent <= 10) return TOP_SPOT_BATTERY_VERY_LOW;
    if (percent <= 20) return TOP_SPOT_BATTERY_LOW;
    return TOP_SPOT_BATTERY_NORMAL;
}
