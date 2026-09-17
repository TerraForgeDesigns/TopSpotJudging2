#include "record_store.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "p4_file_store.h"
#include "p4_storage.h"

static const char *TAG = "top_spot_records";
static const char *SEQUENCE_PATH = "/sdcard/topspot/system/record_sequence.txt";
static const char *RECORD_VERSION = "1";
#define RECORD_INTERNAL_FALLBACK_LIMIT 4096

static SemaphoreHandle_t s_record_mutex;
static top_spot_app_state_t *s_state;
static uint32_t s_next_sequence = 1;
static bool s_ready;

static esp_err_t ensure_mutex(void)
{
    if (s_record_mutex == NULL) {
        s_record_mutex = xSemaphoreCreateMutex();
        if (s_record_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void lock_records(void)
{
    if (s_record_mutex != NULL) {
        xSemaphoreTake(s_record_mutex, portMAX_DELAY);
    }
}

static void unlock_records(void)
{
    if (s_record_mutex != NULL) {
        xSemaphoreGive(s_record_mutex);
    }
}

static void copy_string(char *dest, size_t dest_size, const char *value)
{
    if (dest_size == 0) {
        return;
    }
    if (value == NULL) {
        value = "";
    }
    snprintf(dest, dest_size, "%s", value);
}

static void *record_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL && size <= RECORD_INTERNAL_FALLBACK_LIMIT) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static const char *json_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool json_bool(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsTrue(item);
}

static int json_int(cJSON *root, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool has_suffix(const char *value, const char *suffix)
{
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len && strcmp(value + value_len - suffix_len, suffix) == 0;
}

static bool is_terminal_sync_state(const char *sync_state)
{
    return strcmp(sync_state, TOP_SPOT_SYNC_SYNCED) == 0 ||
           strcmp(sync_state, TOP_SPOT_SYNC_LEGACY) == 0 ||
           strcmp(sync_state, TOP_SPOT_SYNC_REJECTED) == 0;
}

static bool local_id_was_attempted(const char *local_record_id,
                                   const char attempted_ids[][16],
                                   size_t attempted_count)
{
    if (attempted_ids == NULL) {
        return false;
    }
    for (size_t i = 0; i < attempted_count; i++) {
        if (strcmp(local_record_id, attempted_ids[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void sanitize_entry(const char *entry, char *dest, size_t dest_size)
{
    size_t used = 0;
    if (dest_size == 0) {
        return;
    }

    for (const char *p = entry; *p != '\0' && used + 1 < dest_size; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch)) {
            dest[used++] = (char)tolower(ch);
        } else if ((ch == '-' || ch == '_') && used > 0) {
            dest[used++] = (char)ch;
        }
    }

    if (used == 0) {
        copy_string(dest, dest_size, "unknown");
    } else {
        dest[used] = '\0';
    }
}

static void parse_local_id_sequence(const char *local_record_id, uint32_t *max_sequence)
{
    if (local_record_id == NULL || strncmp(local_record_id, "local-", 6) != 0) {
        return;
    }
    char *end = NULL;
    unsigned long value = strtoul(local_record_id + 6, &end, 10);
    if (end != local_record_id + 6 && *end == '\0' && value > *max_sequence) {
        *max_sequence = (uint32_t)value;
    }
}

static esp_err_t load_sequence_file(uint32_t *sequence)
{
    FILE *file = fopen(SEQUENCE_PATH, "r");
    if (file == NULL) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    unsigned long value = 0;
    int matched = fscanf(file, "%lu", &value);
    fclose(file);
    if (matched == 1 && value > 0) {
        *sequence = (uint32_t)value;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t save_sequence_file(uint32_t sequence)
{
    FILE *file = fopen(SEQUENCE_PATH, "w");
    if (file == NULL) {
        ESP_LOGW(TAG, "Unable to update record sequence %s: errno=%d", SEQUENCE_PATH, errno);
        return ESP_FAIL;
    }

    fprintf(file, "%lu\n", (unsigned long)sequence);
    if (fflush(file) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    fsync(fileno(file));
    return fclose(file) == 0 ? ESP_OK : ESP_FAIL;
}

static cJSON *record_to_json(const top_spot_record_t *record)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "schema_version", RECORD_VERSION);
    cJSON_AddStringToObject(root, "local_record_id", record->local_record_id);
    cJSON_AddStringToObject(root, "handheld_id", record->handheld_id);
    cJSON_AddNumberToObject(root, "show_id", record->show_id);
    cJSON_AddStringToObject(root, "show_name", record->show_name);
    cJSON_AddNumberToObject(root, "show_config_revision", record->show_config_revision);
    cJSON_AddNumberToObject(root, "closed_at_uptime_ms", record->closed_at_uptime_ms);
    cJSON_AddNumberToObject(root, "score_range_max", record->score_range_max);
    cJSON_AddStringToObject(root, "entry_number", record->entry_number);
    cJSON_AddStringToObject(root, "participant", record->participant);
    cJSON_AddStringToObject(root, "year", record->year);
    cJSON_AddStringToObject(root, "make", record->make);
    cJSON_AddStringToObject(root, "model", record->model);

    cJSON *scores = cJSON_AddArrayToObject(root, "scores");
    for (int i = 0; i < record->score_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        cJSON *score = cJSON_CreateObject();
        cJSON_AddNumberToObject(score, "id", record->score_category_ids[i]);
        cJSON_AddNumberToObject(score, "category_id", record->score_category_ids[i]);
        cJSON_AddStringToObject(score, "category", record->score_category_names[i]);
        cJSON_AddNumberToObject(score, "score", record->scores[i]);
        cJSON_AddItemToArray(scores, score);
    }

    cJSON_AddNumberToObject(root, "total_score", top_spot_total_score(record));
    cJSON_AddNumberToObject(root, "max_score", top_spot_record_max_score(record));

    cJSON *award_options = cJSON_AddArrayToObject(root, "award_options");
    for (int i = 0; i < record->award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        cJSON *award = cJSON_CreateObject();
        cJSON_AddNumberToObject(award, "id", record->award_ids[i]);
        cJSON_AddStringToObject(award, "name", record->award_names[i]);
        cJSON_AddItemToArray(award_options, award);
    }

    cJSON *awards = cJSON_AddArrayToObject(root, "award_nominations");
    for (int i = 0; i < record->award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        if (record->nominations[i]) {
            cJSON *award = cJSON_CreateObject();
            cJSON_AddNumberToObject(award, "id", record->award_ids[i]);
            cJSON_AddStringToObject(award, "name", record->award_names[i]);
            cJSON_AddItemToArray(awards, award);
        }
    }

    cJSON *photos = cJSON_AddObjectToObject(root, "photos");
    cJSON_AddStringToObject(photos, "vehicle_photo_path", record->vehicle_photo_path);
    cJSON_AddStringToObject(photos, "judge_sheet_photo_path", record->judge_sheet_photo_path);
    cJSON_AddBoolToObject(photos, "vehicle_photo_captured", record->vehicle_photo_captured);
    cJSON_AddBoolToObject(photos, "judge_sheet_photo_captured", record->judge_sheet_photo_captured);
    cJSON_AddBoolToObject(photos, "vehicle_photo_missing", record->vehicle_photo_missing);
    cJSON_AddBoolToObject(photos, "judge_sheet_photo_missing", record->judge_sheet_photo_missing);

    cJSON_AddBoolToObject(root, "completed", record->submitted);
    cJSON_AddBoolToObject(root, "submitted", record->submitted);

    cJSON *sync = cJSON_AddObjectToObject(root, "sync");
    cJSON_AddStringToObject(sync, "sync_state", record->sync_state);
    cJSON_AddNumberToObject(sync, "retry_count", record->retry_count);
    cJSON_AddStringToObject(sync, "last_error", record->last_error);

    return root;
}

static esp_err_t parse_record_json(const char *path, const char *json, top_spot_record_t *record)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGW(TAG, "Skipping malformed record JSON: %s", path);
        return ESP_ERR_INVALID_ARG;
    }

    memset(record, 0, sizeof(*record));
    copy_string(record->local_record_id, sizeof(record->local_record_id), json_string(root, "local_record_id"));
    copy_string(record->handheld_id, sizeof(record->handheld_id), json_string(root, "handheld_id"));
    record->show_id = json_int(root, "show_id", 0);
    copy_string(record->show_name, sizeof(record->show_name), json_string(root, "show_name"));
    record->show_config_revision = json_int(root, "show_config_revision", 0);
    record->closed_at_uptime_ms = json_int(root, "closed_at_uptime_ms", 0);
    record->score_min = 1;
    record->score_range_max = json_int(root, "score_range_max", 0);
    record->max_score = json_int(root, "max_score", 0);
    copy_string(record->entry_number, sizeof(record->entry_number), json_string(root, "entry_number"));
    copy_string(record->participant, sizeof(record->participant), json_string(root, "participant"));
    copy_string(record->year, sizeof(record->year), json_string(root, "year"));
    copy_string(record->make, sizeof(record->make), json_string(root, "make"));
    copy_string(record->model, sizeof(record->model), json_string(root, "model"));
    record->submitted = json_bool(root, "submitted") || json_bool(root, "completed");
    if (record->local_record_id[0] == '\0' || !record->submitted) {
        ESP_LOGW(TAG, "Skipping incomplete record: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *scores = cJSON_GetObjectItemCaseSensitive(root, "scores");
    if (!cJSON_IsArray(scores) || cJSON_GetArraySize(scores) == 0) {
        ESP_LOGW(TAG, "Skipping record with invalid scores: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    int score_size = cJSON_GetArraySize(scores);
    for (int i = 0; i < score_size && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        cJSON *score = cJSON_GetArrayItem(scores, i);
        int category_id = json_int(score, "category_id", json_int(score, "id", i + 1));
        record->score_category_ids[i] = category_id;
        copy_string(record->score_category_names[i], sizeof(record->score_category_names[i]),
                    json_string(score, "category"));
        if (record->score_category_names[i][0] == '\0') {
            snprintf(record->score_category_names[i], sizeof(record->score_category_names[i]),
                     "Category %d", category_id);
        }
        record->scores[i] = json_int(score, "score", 0);
        record->score_count++;
    }
    if (record->score_range_max <= 0) {
        record->score_range_max = record->score_count > 0 && record->max_score > 0
                                      ? record->max_score / record->score_count
                                      : TOP_SPOT_DEFAULT_SCORE_MAX;
    }
    if (record->score_range_max <= 0 || record->score_range_max > TOP_SPOT_MAX_SCORE_RANGE) {
        record->score_range_max = TOP_SPOT_DEFAULT_SCORE_MAX;
    }
    if (record->max_score <= 0) {
        record->max_score = top_spot_record_max_score(record);
    }

    cJSON *award_options = cJSON_GetObjectItemCaseSensitive(root, "award_options");
    if (cJSON_IsArray(award_options)) {
        cJSON *award = NULL;
        cJSON_ArrayForEach(award, award_options) {
            if (record->award_count >= TOP_SPOT_MAX_AWARDS) {
                break;
            }
            int index = record->award_count++;
            record->award_ids[index] = json_int(award, "id", 0);
            copy_string(record->award_names[index], sizeof(record->award_names[index]),
                        json_string(award, "name"));
        }
    }

    cJSON *awards = cJSON_GetObjectItemCaseSensitive(root, "award_nominations");
    if (cJSON_IsArray(awards)) {
        cJSON *award = NULL;
        cJSON_ArrayForEach(award, awards) {
            int id = json_int(award, "id", 0);
            bool matched = false;
            for (int i = 0; i < record->award_count; i++) {
                if (record->award_ids[i] == id) {
                    record->nominations[i] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched && record->award_count < TOP_SPOT_MAX_AWARDS) {
                int index = record->award_count++;
                record->award_ids[index] = id;
                copy_string(record->award_names[index], sizeof(record->award_names[index]),
                            json_string(award, "name"));
                record->nominations[index] = true;
            }
        }
    }

    cJSON *photos = cJSON_GetObjectItemCaseSensitive(root, "photos");
    if (cJSON_IsObject(photos)) {
        copy_string(record->vehicle_photo_path, sizeof(record->vehicle_photo_path),
                    json_string(photos, "vehicle_photo_path"));
        copy_string(record->judge_sheet_photo_path, sizeof(record->judge_sheet_photo_path),
                    json_string(photos, "judge_sheet_photo_path"));
        record->vehicle_photo_captured = json_bool(photos, "vehicle_photo_captured");
        record->judge_sheet_photo_captured = json_bool(photos, "judge_sheet_photo_captured");
        record->vehicle_photo_missing = json_bool(photos, "vehicle_photo_missing");
        record->judge_sheet_photo_missing = json_bool(photos, "judge_sheet_photo_missing");
    }

    cJSON *sync = cJSON_GetObjectItemCaseSensitive(root, "sync");
    if (cJSON_IsObject(sync)) {
        copy_string(record->sync_state, sizeof(record->sync_state), json_string(sync, "sync_state"));
        record->retry_count = json_int(sync, "retry_count", 0);
        copy_string(record->last_error, sizeof(record->last_error), json_string(sync, "last_error"));
    }
    if (record->sync_state[0] == '\0') {
        copy_string(record->sync_state, sizeof(record->sync_state), TOP_SPOT_SYNC_PENDING);
    }
    copy_string(record->file_path, sizeof(record->file_path), path);

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t read_text_file(const char *path, char **out_text)
{
    *out_text = NULL;
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return ESP_FAIL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    long size = ftell(file);
    if (size < 0 || size > 32768) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(file);

    char *text = record_alloc((size_t)size + 1);
    if (text == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (read_len != (size_t)size) {
        free(text);
        return ESP_FAIL;
    }

    *out_text = text;
    return ESP_OK;
}

static bool state_has_record_id(const top_spot_app_state_t *state, const char *local_record_id)
{
    for (int i = 0; i < state->saved_count; i++) {
        if (strcmp(state->saved[i].local_record_id, local_record_id) == 0) {
            return true;
        }
    }
    return false;
}

static void check_photo_path(top_spot_record_t *record, const char *path, bool *missing)
{
    *missing = false;
    if (path[0] == '\0') {
        return;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        *missing = true;
        ESP_LOGW(TAG, "Record %s references missing photo: %s",
                 record->local_record_id, path);
    }
}

static esp_err_t load_records_locked(top_spot_app_state_t *state)
{
    DIR *dir = opendir(TOP_SPOT_RECORDS_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Unable to open records directory %s: errno=%d", TOP_SPOT_RECORDS_DIR, errno);
        return ESP_FAIL;
    }

    state->saved_count = 0;
    uint32_t max_sequence = 0;
    int malformed = 0;
    int overflow = 0;

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char path[256];
        if (has_suffix(entry->d_name, ".json")) {
            snprintf(path, sizeof(path), "%s/%s", TOP_SPOT_RECORDS_DIR, entry->d_name);
            top_spot_file_recover_atomic(path, TAG);
        } else if (has_suffix(entry->d_name, ".json.tmp") || has_suffix(entry->d_name, ".json.bak")) {
            char final_name[128];
            size_t name_len = strlen(entry->d_name);
            if (name_len >= 4 && name_len - 4 < sizeof(final_name)) {
                memcpy(final_name, entry->d_name, name_len - 4);
                final_name[name_len - 4] = '\0';
                snprintf(path, sizeof(path), "%s/%s", TOP_SPOT_RECORDS_DIR, final_name);
                top_spot_file_recover_atomic(path, TAG);
            }
        }
    }
    rewinddir(dir);

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !has_suffix(entry->d_name, ".json") ||
            strstr(entry->d_name, ".tmp") != NULL || strstr(entry->d_name, ".bak") != NULL) {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", TOP_SPOT_RECORDS_DIR, entry->d_name);

        char *json = NULL;
        esp_err_t err = read_text_file(path, &json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Skipping unreadable record %s: %s", path, esp_err_to_name(err));
            malformed++;
            continue;
        }

        top_spot_record_t loaded;
        err = parse_record_json(path, json, &loaded);
        free(json);
        if (err != ESP_OK) {
            malformed++;
            continue;
        }

        parse_local_id_sequence(loaded.local_record_id, &max_sequence);
        check_photo_path(&loaded, loaded.vehicle_photo_path, &loaded.vehicle_photo_missing);
        check_photo_path(&loaded, loaded.judge_sheet_photo_path, &loaded.judge_sheet_photo_missing);

        if (state_has_record_id(state, loaded.local_record_id)) {
            ESP_LOGW(TAG, "Skipping duplicate local_record_id %s from %s",
                     loaded.local_record_id, path);
            continue;
        }
        if (state->saved_count >= TOP_SPOT_MAX_SAVED_RECORDS) {
            overflow++;
            continue;
        }

        state->saved[state->saved_count++] = loaded;
    }

    closedir(dir);

    uint32_t sequence_file = 1;
    esp_err_t seq_err = load_sequence_file(&sequence_file);
    if (seq_err != ESP_OK && seq_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Record sequence file invalid; rebuilding from records");
    }
    s_next_sequence = sequence_file;
    if (s_next_sequence <= max_sequence) {
        s_next_sequence = max_sequence + 1;
    }
    if (s_next_sequence == 0) {
        s_next_sequence = 1;
    }
    save_sequence_file(s_next_sequence);

    ESP_LOGI(TAG, "Loaded %d persisted record%s from %s; malformed=%d overflow=%d next_id=%lu",
             state->saved_count,
             state->saved_count == 1 ? "" : "s",
             TOP_SPOT_RECORDS_DIR,
             malformed,
             overflow,
             (unsigned long)s_next_sequence);
    return ESP_OK;
}

esp_err_t top_spot_record_store_init(top_spot_app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    lock_records();
    s_state = state;
    s_ready = false;
    if (!top_spot_storage_is_ready()) {
        ESP_LOGW(TAG, "Record store unavailable because storage is not ready");
        unlock_records();
        return ESP_ERR_INVALID_STATE;
    }

    err = load_records_locked(state);
    s_ready = (err == ESP_OK);
    unlock_records();
    return err;
}

static esp_err_t write_json_atomically(const char *final_path, const char *json)
{
    struct stat st;
    if (stat(final_path, &st) == 0) {
        ESP_LOGE(TAG, "Refusing to overwrite existing record %s", final_path);
        return ESP_ERR_INVALID_STATE;
    }
    return top_spot_file_write_text_atomic(final_path, json, TAG);
}

esp_err_t top_spot_record_store_save(top_spot_record_t *record, top_spot_app_state_t *state)
{
    if (record == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_records();
    if (!s_ready || !top_spot_storage_is_ready()) {
        unlock_records();
        return ESP_ERR_INVALID_STATE;
    }
    if (state->saved_count >= TOP_SPOT_MAX_SAVED_RECORDS) {
        unlock_records();
        return ESP_ERR_NO_MEM;
    }

    uint32_t sequence = s_next_sequence++;
    snprintf(record->local_record_id, sizeof(record->local_record_id), "local-%08lu", (unsigned long)sequence);
    record->closed_at_uptime_ms = (int)sequence;
    record->submitted = true;
    copy_string(record->sync_state, sizeof(record->sync_state), TOP_SPOT_SYNC_PENDING);
    record->retry_count = 0;
    record->last_error[0] = '\0';
    record->vehicle_photo_missing = false;
    record->judge_sheet_photo_missing = false;

    char entry[24];
    sanitize_entry(record->entry_number, entry, sizeof(entry));

    char final_path[256];
    snprintf(final_path, sizeof(final_path), "%s/record_%s_%08lu.json",
             TOP_SPOT_RECORDS_DIR, entry, (unsigned long)sequence);

    cJSON *root = record_to_json(record);
    if (root == NULL) {
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        unlock_records();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = write_json_atomically(final_path, json);
    cJSON_free(json);
    if (err != ESP_OK) {
        s_next_sequence = sequence;
        unlock_records();
        return err;
    }

    copy_string(record->file_path, sizeof(record->file_path), final_path);
    state->saved[state->saved_count++] = *record;
    save_sequence_file(s_next_sequence);
    unlock_records();
    return ESP_OK;
}

esp_err_t top_spot_record_store_mark_sync_result(const char *local_record_id, bool success, const char *error)
{
    if (local_record_id == NULL || local_record_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    lock_records();
    if (s_state == NULL || !s_ready || !top_spot_storage_is_ready()) {
        unlock_records();
        return ESP_ERR_INVALID_STATE;
    }

    top_spot_record_t *record = NULL;
    for (int i = 0; i < s_state->saved_count; i++) {
        if (strcmp(s_state->saved[i].local_record_id, local_record_id) == 0) {
            record = &s_state->saved[i];
            break;
        }
    }
    if (record == NULL || record->file_path[0] == '\0') {
        unlock_records();
        return ESP_ERR_NOT_FOUND;
    }

    top_spot_record_t *updated = record_alloc(sizeof(*updated));
    if (updated == NULL) {
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    *updated = *record;
    if (success) {
        copy_string(updated->sync_state, sizeof(updated->sync_state), TOP_SPOT_SYNC_SYNCED);
        updated->last_error[0] = '\0';
    } else {
        copy_string(updated->sync_state, sizeof(updated->sync_state), TOP_SPOT_SYNC_FAILED);
        updated->retry_count++;
        copy_string(updated->last_error, sizeof(updated->last_error), error != NULL ? error : "sync failed");
    }

    cJSON *root = record_to_json(updated);
    if (root == NULL) {
        free(updated);
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        free(updated);
        unlock_records();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = top_spot_file_write_text_atomic(record->file_path, json, TAG);
    cJSON_free(json);
    if (err == ESP_OK) {
        *record = *updated;
    }
    free(updated);
    ESP_LOGI(TAG, "Record %s sync_state=%s err=%s",
             record->local_record_id, record->sync_state, esp_err_to_name(err));
    unlock_records();
    return err;
}

esp_err_t top_spot_record_store_mark_legacy(const char *local_record_id, const char *reason)
{
    if (local_record_id == NULL || local_record_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    lock_records();
    if (s_state == NULL || !s_ready || !top_spot_storage_is_ready()) {
        unlock_records();
        return ESP_ERR_INVALID_STATE;
    }

    top_spot_record_t *record = NULL;
    for (int i = 0; i < s_state->saved_count; i++) {
        if (strcmp(s_state->saved[i].local_record_id, local_record_id) == 0) {
            record = &s_state->saved[i];
            break;
        }
    }
    if (record == NULL || record->file_path[0] == '\0') {
        unlock_records();
        return ESP_ERR_NOT_FOUND;
    }

    top_spot_record_t *updated = record_alloc(sizeof(*updated));
    if (updated == NULL) {
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    *updated = *record;
    copy_string(updated->sync_state, sizeof(updated->sync_state), TOP_SPOT_SYNC_LEGACY);
    copy_string(updated->last_error, sizeof(updated->last_error),
                reason != NULL ? reason : "legacy record cannot be synced safely");

    cJSON *root = record_to_json(updated);
    if (root == NULL) {
        free(updated);
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        free(updated);
        unlock_records();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = top_spot_file_write_text_atomic(record->file_path, json, TAG);
    cJSON_free(json);
    if (err == ESP_OK) {
        *record = *updated;
    }
    free(updated);
    ESP_LOGW(TAG, "Record %s is legacy/unsyncable: %s err=%s",
             local_record_id, reason != NULL ? reason : "missing required sync metadata",
             esp_err_to_name(err));
    unlock_records();
    return err;
}

esp_err_t top_spot_record_store_mark_rejected(const char *local_record_id, const char *reason)
{
    if (local_record_id == NULL || local_record_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    lock_records();
    if (s_state == NULL || !s_ready || !top_spot_storage_is_ready()) {
        unlock_records();
        return ESP_ERR_INVALID_STATE;
    }

    top_spot_record_t *record = NULL;
    for (int i = 0; i < s_state->saved_count; i++) {
        if (strcmp(s_state->saved[i].local_record_id, local_record_id) == 0) {
            record = &s_state->saved[i];
            break;
        }
    }
    if (record == NULL || record->file_path[0] == '\0') {
        unlock_records();
        return ESP_ERR_NOT_FOUND;
    }

    top_spot_record_t *updated = record_alloc(sizeof(*updated));
    if (updated == NULL) {
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    *updated = *record;
    copy_string(updated->sync_state, sizeof(updated->sync_state), TOP_SPOT_SYNC_REJECTED);
    updated->retry_count++;
    copy_string(updated->last_error, sizeof(updated->last_error),
                reason != NULL ? reason : "Home Base rejected record");

    cJSON *root = record_to_json(updated);
    if (root == NULL) {
        free(updated);
        unlock_records();
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        free(updated);
        unlock_records();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = top_spot_file_write_text_atomic(record->file_path, json, TAG);
    cJSON_free(json);
    if (err == ESP_OK) {
        *record = *updated;
    }
    free(updated);
    ESP_LOGW(TAG, "Record %s sync_state=%s rejected=%s err=%s",
             local_record_id, TOP_SPOT_SYNC_REJECTED,
             reason != NULL ? reason : "Home Base rejected record",
             esp_err_to_name(err));
    unlock_records();
    return err;
}

esp_err_t top_spot_record_store_get_pending(top_spot_record_t *out_record)
{
    return top_spot_record_store_get_pending_excluding(out_record, NULL, 0);
}

esp_err_t top_spot_record_store_get_pending_excluding(top_spot_record_t *out_record,
                                                      const char attempted_ids[][16],
                                                      size_t attempted_count)
{
    if (out_record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_records();
    if (s_state == NULL || !s_ready) {
        unlock_records();
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < s_state->saved_count; i++) {
        if (!is_terminal_sync_state(s_state->saved[i].sync_state) &&
            !local_id_was_attempted(s_state->saved[i].local_record_id, attempted_ids, attempted_count)) {
            *out_record = s_state->saved[i];
            unlock_records();
            return ESP_OK;
        }
    }
    unlock_records();
    return ESP_ERR_NOT_FOUND;
}

int top_spot_record_store_count(void)
{
    int count = 0;
    lock_records();
    if (s_state != NULL) {
        count = s_state->saved_count;
    }
    unlock_records();
    return count;
}

int top_spot_record_store_pending_count(void)
{
    int count = 0;
    lock_records();
    if (s_state != NULL) {
        for (int i = 0; i < s_state->saved_count; i++) {
            if (!is_terminal_sync_state(s_state->saved[i].sync_state)) {
                count++;
            }
        }
    }
    unlock_records();
    return count;
}

const top_spot_record_t *top_spot_record_store_get(size_t index)
{
    const top_spot_record_t *record = NULL;
    lock_records();
    if (s_state != NULL && index < (size_t)s_state->saved_count) {
        record = &s_state->saved[index];
    }
    unlock_records();
    return record;
}

bool top_spot_record_store_is_ready(void)
{
    bool ready;
    lock_records();
    ready = s_ready;
    unlock_records();
    return ready;
}
