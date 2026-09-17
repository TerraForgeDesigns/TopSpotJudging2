#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "judging_model.h"

#define TOP_SPOT_RECORDS_DIR "/sdcard/topspot/records"
#define TOP_SPOT_SYNC_PENDING "pending"
#define TOP_SPOT_SYNC_SYNCED "synced"
#define TOP_SPOT_SYNC_FAILED "failed"
#define TOP_SPOT_SYNC_REJECTED "rejected"
#define TOP_SPOT_SYNC_LEGACY "legacy"

esp_err_t top_spot_record_store_init(top_spot_app_state_t *state);
esp_err_t top_spot_record_store_save(top_spot_record_t *record, top_spot_app_state_t *state);
esp_err_t top_spot_record_store_mark_sync_result(const char *local_record_id, bool success, const char *error);
esp_err_t top_spot_record_store_mark_rejected(const char *local_record_id, const char *reason);
esp_err_t top_spot_record_store_mark_legacy(const char *local_record_id, const char *reason);
esp_err_t top_spot_record_store_get_pending(top_spot_record_t *out_record);
esp_err_t top_spot_record_store_get_pending_excluding(top_spot_record_t *out_record,
                                                      const char attempted_ids[][16],
                                                      size_t attempted_count);
int top_spot_record_store_count(void);
int top_spot_record_store_pending_count(void);
const top_spot_record_t *top_spot_record_store_get(size_t index);
bool top_spot_record_store_is_ready(void);
