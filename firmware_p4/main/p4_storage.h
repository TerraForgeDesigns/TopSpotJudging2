#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define TOP_SPOT_STORAGE_ROOT "/topspot"

esp_err_t top_spot_storage_init(void);
bool top_spot_storage_is_ready(void);
uint64_t top_spot_storage_get_total_space(void);
uint64_t top_spot_storage_get_free_space(void);
const char *top_spot_storage_mount_point(void);
const char *top_spot_storage_status_text(void);
