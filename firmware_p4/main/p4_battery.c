#include "p4_battery.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_GPIO 20
#define BATTERY_ATTEN ADC_ATTEN_DB_6
#define BATTERY_BITS ADC_BITWIDTH_12
#define BATTERY_SAMPLES 32
#define BATTERY_PERIOD_MS 2000
#define BATTERY_LOG_CYCLES 15

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_channel;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static top_spot_battery_status_t s_status;

void top_spot_battery_get_status(top_spot_battery_status_t *status)
{
    if (status == NULL) return;
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t read_average_mv(int *average_mv)
{
    int raw;
    /* Discard the first conversion after idle, then space the retained samples. */
    esp_err_t err = adc_oneshot_read(s_adc, s_channel, &raw);
    if (err != ESP_OK) return err;
    int sum = 0;
    for (int i = 0; i < BATTERY_SAMPLES; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        err = adc_oneshot_read(s_adc, s_channel, &raw);
        if (err != ESP_OK) return err;
        int mv;
        err = adc_cali_raw_to_voltage(s_cali, raw, &mv);
        if (err != ESP_OK) return err;
        sum += mv;
    }
    *average_mv = (sum + BATTERY_SAMPLES / 2) / BATTERY_SAMPLES;
    return ESP_OK;
}

static void battery_task(void *arg)
{
    (void)arg;
    top_spot_battery_filter_t filter = {0};
    TickType_t last_wake = xTaskGetTickCount();
    unsigned log_cycle = 0;
    bool was_valid = false;
    for (;;) {
        int average_mv;
        esp_err_t err = read_average_mv(&average_mv);
        top_spot_battery_status_t next = {0};
        if (err == ESP_OK) {
            next.valid = true;
            next.adc_mv = top_spot_battery_filter_mv(&filter, average_mv);
            int raw_voltage_mv = top_spot_battery_voltage_mv(next.adc_mv);
            next.voltage_mv = top_spot_battery_correct_voltage_mv(raw_voltage_mv);
            next.percent = top_spot_battery_percent(next.voltage_mv);
            next.level = top_spot_battery_level(next.percent);
            if (log_cycle == 0 || !was_valid) {
                ESP_LOGI(TAG, "BATTERY: adc_mv=%d raw_voltage=%.3fV corrected_voltage=%.3fV percent=%d",
                         next.adc_mv, raw_voltage_mv / 1000.0, next.voltage_mv / 1000.0, next.percent);
            }
        } else {
            /* Do not present stale readings as current, or filter across an outage. */
            filter.initialized = false;
            if (log_cycle == 0 || was_valid) {
                ESP_LOGW(TAG, "Battery sample unavailable: %s", esp_err_to_name(err));
            }
        }
        portENTER_CRITICAL(&s_lock);
        s_status = next;
        portEXIT_CRITICAL(&s_lock);
        was_valid = next.valid;
        log_cycle = (log_cycle + 1) % BATTERY_LOG_CYCLES;
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BATTERY_PERIOD_MS));
    }
}

esp_err_t top_spot_battery_init(void)
{
    if (s_adc != NULL) return ESP_ERR_INVALID_STATE;
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(BATTERY_GPIO, &unit, &s_channel);
    if (err != ESP_OK) return err;
    /* Guard the board mapping: ESP32-P4 GPIO20 is ADC1 channel 4. */
    if (unit != ADC_UNIT_1 || s_channel != ADC_CHANNEL_4) return ESP_ERR_INVALID_ARG;
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) return err;
    const adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = BATTERY_ATTEN,
        .bitwidth = BATTERY_BITS,
    };
    err = adc_oneshot_config_channel(s_adc, s_channel, &channel_cfg);
    if (err != ESP_OK) goto fail;
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = BATTERY_ATTEN,
        .bitwidth = BATTERY_BITS,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) goto fail;
    if (xTaskCreate(battery_task, "battery", 3072, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    return ESP_OK;

fail:
    if (s_cali != NULL) {
        adc_cali_delete_scheme_curve_fitting(s_cali);
        s_cali = NULL;
    }
    adc_oneshot_del_unit(s_adc);
    s_adc = NULL;
    return err;
}
