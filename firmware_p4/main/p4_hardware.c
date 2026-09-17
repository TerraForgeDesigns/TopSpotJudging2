#include "p4_hardware.h"

#include "bsp/touch.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "esp_lv_adapter_input.h"

static const char *TAG = "top_spot_hw";
static const uint16_t DISPLAY_DRAW_BUFFER_HEIGHT = 20;

static void log_heap_caps(const char *label, uint32_t caps)
{
    ESP_LOGI(TAG, "DISPLAY MEM: %s free=%u largest=%u",
             label,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
}

lv_display_t *p4_hardware_start_display(void)
{
    log_heap_caps("before display INTERNAL", MALLOC_CAP_INTERNAL);
    log_heap_caps("before display DMA", MALLOC_CAP_DMA);
    log_heap_caps("before display 8BIT", MALLOC_CAP_8BIT);
    log_heap_caps("before display SPIRAM", MALLOC_CAP_SPIRAM);

    const esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    esp_err_t err = esp_lv_adapter_init(&adapter_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    bsp_lcd_handles_t lcd_panels;
    err = bsp_display_new_with_handles(NULL, &lcd_panels);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display panel init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    const esp_lv_adapter_display_config_t display_cfg = {
        .panel = lcd_panels.panel,
        .panel_io = lcd_panels.io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = DISPLAY_DRAW_BUFFER_HEIGHT,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = false,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    ESP_LOGI(TAG, "DISPLAY MEM: LVGL draw buffer request %u x %u x 2 = %u bytes",
             (unsigned)BSP_LCD_H_RES,
             (unsigned)DISPLAY_DRAW_BUFFER_HEIGHT,
             (unsigned)(BSP_LCD_H_RES * DISPLAY_DRAW_BUFFER_HEIGHT * 2));

    lv_display_t *display = esp_lv_adapter_register_display(&display_cfg);
    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        return NULL;
    }

    esp_lcd_touch_handle_t touch;
    const bsp_touch_config_t touch_config = {
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    err = bsp_touch_new(&touch_config, &touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);
    lv_indev_t *indev = esp_lv_adapter_register_touch(&touch_cfg);
    if (indev == NULL) {
        ESP_LOGE(TAG, "LVGL touch registration failed");
        return NULL;
    }

    err = esp_lv_adapter_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed: %s", esp_err_to_name(err));
        return NULL;
    }

    if (display != NULL) {
        bsp_display_backlight_on();
    }

    log_heap_caps("after display INTERNAL", MALLOC_CAP_INTERNAL);
    log_heap_caps("after display DMA", MALLOC_CAP_DMA);
    log_heap_caps("after display 8BIT", MALLOC_CAP_8BIT);
    log_heap_caps("after display SPIRAM", MALLOC_CAP_SPIRAM);

    return display;
}
