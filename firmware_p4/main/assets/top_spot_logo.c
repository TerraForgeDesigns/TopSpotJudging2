#include "top_spot_logo.h"

#include <stdint.h>

extern const uint8_t top_spot_logo_png_start[] asm("_binary_top_spot_logo_png_start");

const lv_image_dsc_t top_spot_logo = {
    .header.cf = LV_COLOR_FORMAT_RAW_ALPHA,
    .data_size = 2747147,
    .data = top_spot_logo_png_start,
};
