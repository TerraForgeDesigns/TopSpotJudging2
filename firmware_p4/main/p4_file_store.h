#pragma once

#include "esp_err.h"

esp_err_t top_spot_file_recover_atomic(const char *final_path, const char *log_tag);
esp_err_t top_spot_file_write_text_atomic(const char *final_path, const char *text, const char *log_tag);
