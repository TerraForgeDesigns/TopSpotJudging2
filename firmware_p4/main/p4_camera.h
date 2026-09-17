#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#define TOP_SPOT_CAMERA_WIDTH 800
#define TOP_SPOT_CAMERA_HEIGHT 640

typedef enum {
    TOP_SPOT_PHOTO_VEHICLE,
    TOP_SPOT_PHOTO_JUDGE_SHEET,
} top_spot_photo_type_t;

esp_err_t top_spot_camera_init(void);
bool top_spot_camera_is_ready(void);
const char *top_spot_camera_status_text(void);
esp_err_t top_spot_camera_start_preview(lv_obj_t *canvas);
esp_err_t top_spot_camera_stop_preview(void);
esp_err_t top_spot_camera_capture_jpeg(top_spot_photo_type_t type,
                                       const char *entry_number,
                                       char *out_path,
                                       size_t out_path_size,
                                       size_t *out_file_size);
void top_spot_camera_deinit(void);
