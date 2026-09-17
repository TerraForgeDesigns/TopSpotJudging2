#include "show_store.h"

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

static const char *TAG = "top_spot_show_store";
static const char *SHOW_SCHEMA_VERSION = "1";

static SemaphoreHandle_t s_show_mutex;
static bool s_ready;

static esp_err_t ensure_mutex(void)
{
    if (s_show_mutex == NULL) {
        s_show_mutex = xSemaphoreCreateMutex();
        if (s_show_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void lock_show(void)
{
    if (s_show_mutex != NULL) {
        xSemaphoreTake(s_show_mutex, portMAX_DELAY);
    }
}

static void unlock_show(void)
{
    if (s_show_mutex != NULL) {
        xSemaphoreGive(s_show_mutex);
    }
}

static void copy_text(char *dest, size_t dest_size, const char *value)
{
    if (dest_size == 0) {
        return;
    }
    snprintf(dest, dest_size, "%s", value != NULL ? value : "");
}

static int json_int(cJSON *root, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static const char *json_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool has_suffix(const char *value, const char *suffix)
{
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len && strcmp(value + value_len - suffix_len, suffix) == 0;
}

static cJSON *show_to_json(const top_spot_show_config_t *show)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "schema_version", SHOW_SCHEMA_VERSION);
    cJSON_AddNumberToObject(root, "show_id", show->show_id);
    cJSON_AddStringToObject(root, "show_name", show->show_name);
    cJSON_AddStringToObject(root, "show_date", show->show_date);
    cJSON_AddNumberToObject(root, "config_revision", show->config_revision);
    cJSON_AddNumberToObject(root, "data_revision", show->data_revision);
    cJSON_AddNumberToObject(root, "score_min", show->score_min);
    cJSON_AddNumberToObject(root, "score_max", show->score_max);
    cJSON_AddNumberToObject(root, "score_range_max", show->score_max);

    cJSON *categories = cJSON_AddArrayToObject(root, "categories");
    for (int i = 0; i < show->category_count; i++) {
        cJSON *category = cJSON_CreateObject();
        cJSON_AddNumberToObject(category, "id", show->categories[i].id);
        cJSON_AddStringToObject(category, "key", show->categories[i].key);
        cJSON_AddStringToObject(category, "name", show->categories[i].name);
        cJSON_AddNumberToObject(category, "sort_order", show->categories[i].sort_order);
        cJSON_AddItemToArray(categories, category);
    }

    cJSON *awards = cJSON_AddArrayToObject(root, "awards");
    for (int i = 0; i < show->award_count; i++) {
        cJSON *award = cJSON_CreateObject();
        cJSON_AddNumberToObject(award, "id", show->awards[i].id);
        cJSON_AddStringToObject(award, "key", show->awards[i].key);
        cJSON_AddStringToObject(award, "name", show->awards[i].name);
        cJSON_AddItemToArray(awards, award);
    }
    return root;
}

static esp_err_t parse_show_json(const char *json, top_spot_show_config_t *show)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(show, 0, sizeof(*show));
    show->loaded = true;
    show->show_id = json_int(root, "show_id", 0);
    copy_text(show->show_name, sizeof(show->show_name), json_string(root, "show_name"));
    copy_text(show->show_date, sizeof(show->show_date), json_string(root, "show_date"));
    show->config_revision = json_int(root, "config_revision", 0);
    show->data_revision = json_int(root, "data_revision", 0);
    show->score_min = json_int(root, "score_min", 1);
    show->score_max = json_int(root, "score_range_max", json_int(root, "score_max", TOP_SPOT_DEFAULT_SCORE_MAX));
    if (show->score_max <= 0 || show->score_max > TOP_SPOT_MAX_SCORE_RANGE) {
        show->score_max = TOP_SPOT_DEFAULT_SCORE_MAX;
    }

    cJSON *categories = cJSON_GetObjectItemCaseSensitive(root, "categories");
    if (cJSON_IsArray(categories)) {
        cJSON *category = NULL;
        cJSON_ArrayForEach(category, categories) {
            if (show->category_count >= TOP_SPOT_MAX_CATEGORIES) {
                break;
            }
            top_spot_category_t *dest = &show->categories[show->category_count++];
            dest->id = json_int(category, "id", 0);
            copy_text(dest->key, sizeof(dest->key), json_string(category, "key"));
            copy_text(dest->name, sizeof(dest->name), json_string(category, "name"));
            dest->sort_order = json_int(category, "sort_order", show->category_count - 1);
        }
    }

    cJSON *awards = cJSON_GetObjectItemCaseSensitive(root, "awards");
    if (cJSON_IsArray(awards)) {
        cJSON *award = NULL;
        cJSON_ArrayForEach(award, awards) {
            if (show->award_count >= TOP_SPOT_MAX_AWARDS) {
                break;
            }
            top_spot_award_t *dest = &show->awards[show->award_count++];
            dest->id = json_int(award, "id", 0);
            copy_text(dest->key, sizeof(dest->key), json_string(award, "key"));
            copy_text(dest->name, sizeof(dest->name), json_string(award, "name"));
        }
    }

    cJSON_Delete(root);
    if (show->show_name[0] == '\0' || show->category_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
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
    char *text = heap_caps_calloc(1, (size_t)size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (text == NULL && size < 1024) {
        text = heap_caps_calloc(1, (size_t)size + 1, MALLOC_CAP_8BIT);
    }
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

static void recover_show_cache_files(void)
{
    DIR *dir = opendir(TOP_SPOT_SHOWS_DIR);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Unable to scan show cache for recovery: errno=%d", errno);
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char path[256];
        if (has_suffix(entry->d_name, ".json")) {
            snprintf(path, sizeof(path), "%s/%s", TOP_SPOT_SHOWS_DIR, entry->d_name);
            top_spot_file_recover_atomic(path, TAG);
        } else if (has_suffix(entry->d_name, ".json.tmp") || has_suffix(entry->d_name, ".json.bak")) {
            char final_name[128];
            size_t name_len = strlen(entry->d_name);
            if (name_len >= 4 && name_len - 4 < sizeof(final_name)) {
                memcpy(final_name, entry->d_name, name_len - 4);
                final_name[name_len - 4] = '\0';
                snprintf(path, sizeof(path), "%s/%s", TOP_SPOT_SHOWS_DIR, final_name);
                top_spot_file_recover_atomic(path, TAG);
            }
        }
    }
    closedir(dir);
}

esp_err_t top_spot_show_store_load_active(top_spot_show_config_t *show)
{
    char *json = NULL;
    esp_err_t err = read_text_file(TOP_SPOT_ACTIVE_SHOW_PATH, &json);
    if (err != ESP_OK) {
        return err;
    }
    err = parse_show_json(json, show);
    free(json);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded cached show '%s' rev=%d categories=%d awards=%d",
                 show->show_name, show->config_revision, show->category_count, show->award_count);
    }
    return err;
}

esp_err_t top_spot_show_store_save_active(const top_spot_show_config_t *show)
{
    if (show == NULL || !show->loaded || show->category_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    lock_show();
    cJSON *root = show_to_json(show);
    if (root == NULL) {
        unlock_show();
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        unlock_show();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = top_spot_file_write_text_atomic(TOP_SPOT_ACTIVE_SHOW_PATH, json, TAG);
    if (err == ESP_OK && show->show_id > 0) {
        char final_path[96];
        snprintf(final_path, sizeof(final_path), "%s/show_%d.json", TOP_SPOT_SHOWS_DIR, show->show_id);
        err = top_spot_file_write_text_atomic(final_path, json, TAG);
    }
    cJSON_free(json);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Cached active show '%s' rev=%d", show->show_name, show->config_revision);
    }
    unlock_show();
    return err;
}

esp_err_t top_spot_show_store_init(top_spot_app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    lock_show();
    s_ready = top_spot_storage_is_ready();
    if (s_ready) {
        recover_show_cache_files();
    }
    unlock_show();
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    top_spot_show_config_t loaded;
    err = top_spot_show_store_load_active(&loaded);
    if (err == ESP_OK) {
        state->show = loaded;
        top_spot_apply_show_to_current(&state->current, &state->show);
    } else {
        ESP_LOGW(TAG, "No cached active show loaded: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

bool top_spot_show_store_is_ready(void)
{
    bool ready;
    lock_show();
    ready = s_ready;
    unlock_show();
    return ready;
}
