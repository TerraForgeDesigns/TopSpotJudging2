#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "cJSON.h"

#include "p4_hardware.h"
#include "p4_battery.h"
#include "p4_network.h"
#include "p4_storage.h"
#include "ui_screens.h"

static const char *TAG = "top_spot_p4";

static void *top_spot_cjson_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static void top_spot_cjson_free(void *ptr)
{
    heap_caps_free(ptr);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Top Spot Judging local workflow");
    cJSON_Hooks json_hooks = {
        .malloc_fn = top_spot_cjson_malloc,
        .free_fn = top_spot_cjson_free,
    };
    cJSON_InitHooks(&json_hooks);

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    lv_display_t *display = p4_hardware_start_display();
    ESP_ERROR_CHECK(display != NULL ? ESP_OK : ESP_FAIL);

    esp_err_t storage_err = top_spot_storage_init();
    if (storage_err != ESP_OK) {
        ESP_LOGW(TAG, "Storage unavailable at boot: %s", esp_err_to_name(storage_err));
    }
    esp_err_t network_err = top_spot_network_init();
    if (network_err != ESP_OK) {
        ESP_LOGW(TAG, "Network unavailable at boot: %s", esp_err_to_name(network_err));
    }

    ESP_LOGI(TAG, "BOOT TRACE: before UI state prepare outside LVGL lock");
    top_spot_ui_prepare_state();
    ESP_LOGI(TAG, "BOOT TRACE: after UI state prepare outside LVGL lock");

    ESP_LOGI(TAG, "BOOT TRACE: before LVGL lock for initial UI");
    ESP_ERROR_CHECK(bsp_display_lock(-1) ? ESP_OK : ESP_ERR_TIMEOUT);
    ESP_LOGI(TAG, "BOOT TRACE: acquired LVGL lock for initial UI");
    ESP_LOGI(TAG, "BOOT TRACE: before top_spot_ui_start");
    top_spot_ui_start();
    ESP_LOGI(TAG, "BOOT TRACE: after top_spot_ui_start");
    bsp_display_unlock();
    ESP_LOGI(TAG, "BOOT TRACE: released LVGL lock for initial UI");
    esp_err_t battery_err = top_spot_battery_init();
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor unavailable: %s", esp_err_to_name(battery_err));
    }
}
