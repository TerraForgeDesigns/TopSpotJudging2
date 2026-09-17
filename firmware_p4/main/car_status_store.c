#include "car_status_store.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "p4_storage.h"
#include "p4_file_store.h"
#include "record_store.h"
#include "show_store.h"

static const char *TAG = "top_spot_car_status";
static const char *CAR_CACHE_SCHEMA_VERSION = "1";

static SemaphoreHandle_t s_mutex;
static top_spot_app_state_t *s_state;
static bool s_ready;

typedef struct {
    int count;
    char entries[TOP_SPOT_MAX_SAVED_RECORDS][sizeof(((top_spot_record_t *)0)->entry_number)];
} pending_entry_snapshot_t;

static void collect_pending_entries(pending_entry_snapshot_t *pending);
static int overlay_pending_on_cache(top_spot_car_cache_t *cache, const pending_entry_snapshot_t *pending);

static void *car_status_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL && size <= 4096) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
    }
    if (ptr == NULL) {
        ESP_LOGW(TAG, "CAR STATUS MEM: allocation failed size=%u psram_largest=%u internal_largest=%u",
                 (unsigned)size,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
    return ptr;
}

static const char *alloc_location(const void *ptr)
{
    if (ptr == NULL) {
        return "null";
    }
    return esp_ptr_external_ram(ptr) ? "PSRAM" : "internal";
}

static void log_stack_headroom(const char *label)
{
    UBaseType_t words = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "CAR STATUS STACK: %s high-water=%u words (%u bytes)",
             label, (unsigned)words, (unsigned)(words * sizeof(StackType_t)));
}

static int cache_safe_count(const top_spot_car_cache_t *cache, const char *context)
{
    if (cache == NULL || cache->car_count <= 0) {
        return 0;
    }
    if (cache->car_count > TOP_SPOT_MAX_CACHED_CARS) {
        ESP_LOGW(TAG, "CAR STATUS: %s car_count=%d exceeds max=%d; clamping",
                 context, cache->car_count, TOP_SPOT_MAX_CACHED_CARS);
        return TOP_SPOT_MAX_CACHED_CARS;
    }
    return cache->car_count;
}

static esp_err_t ensure_mutex(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void lock_cache(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock_cache(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void copy_text(char *dest, size_t dest_size, const char *value)
{
    if (dest_size == 0) {
        return;
    }
    snprintf(dest, dest_size, "%s", value != NULL ? value : "");
}

static const char *json_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static int json_int(cJSON *root, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static esp_err_t cache_path(int show_id, char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s/show_%d_cars.json", TOP_SPOT_SHOWS_DIR, show_id);
    if (written < 0 || (size_t)written >= out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t read_text_file(const char *path, char **out_text)
{
    *out_text = NULL;
    ESP_LOGI(TAG, "BOOT TRACE: car cache before fopen %s", path);
    FILE *file = fopen(path, "r");
    ESP_LOGI(TAG, "BOOT TRACE: car cache after fopen file=%p errno=%d", file, errno);
    if (file == NULL) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BOOT TRACE: car cache before fseek end");
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BOOT TRACE: car cache before ftell");
    long size = ftell(file);
    ESP_LOGI(TAG, "BOOT TRACE: car cache after ftell size=%ld", size);
    if (size < 0 || size > 98304) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "BOOT TRACE: car cache before rewind/read alloc bytes=%ld", size);
    rewind(file);
    char *text = heap_caps_calloc(1, (size_t)size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (text == NULL && size < 2048) {
        text = heap_caps_calloc(1, (size_t)size + 1, MALLOC_CAP_8BIT);
    }
    ESP_LOGI(TAG, "BOOT TRACE: car cache after read alloc ptr=%p", text);
    if (text == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "BOOT TRACE: car cache before fread bytes=%ld", size);
    size_t read_len = fread(text, 1, (size_t)size, file);
    ESP_LOGI(TAG, "BOOT TRACE: car cache after fread read=%u expected=%ld", (unsigned)read_len, size);
    fclose(file);
    if (read_len != (size_t)size) {
        free(text);
        return ESP_FAIL;
    }
    *out_text = text;
    return ESP_OK;
}

static void parse_car(top_spot_car_status_t *dest, cJSON *car)
{
    memset(dest, 0, sizeof(*dest));
    dest->id = json_int(car, "id", 0);
    dest->data_revision = json_int(car, "data_revision", 0);
    copy_text(dest->entry_number, sizeof(dest->entry_number), json_string(car, "entry_number"));
    copy_text(dest->participant, sizeof(dest->participant), json_string(car, "participant"));
    copy_text(dest->year, sizeof(dest->year), json_string(car, "year"));
    copy_text(dest->make, sizeof(dest->make), json_string(car, "make"));
    copy_text(dest->model, sizeof(dest->model), json_string(car, "model"));
    copy_text(dest->vehicle_type, sizeof(dest->vehicle_type), json_string(car, "vehicle_type"));
    copy_text(dest->status, sizeof(dest->status), json_string(car, "status"));
    copy_text(dest->judged_source, sizeof(dest->judged_source), json_string(car, "judged_source"));
    copy_text(dest->judged_at, sizeof(dest->judged_at), json_string(car, "judged_at"));
    if (dest->status[0] == '\0') {
        copy_text(dest->status, sizeof(dest->status), TOP_SPOT_CAR_STATUS_UNJUDGED);
    }
}

static int find_by_id_or_entry(top_spot_car_cache_t *cache, const top_spot_car_status_t *car)
{
    int count = cache_safe_count(cache, "find");
    for (int i = 0; i < count; i++) {
        if ((car->id > 0 && cache->cars[i].id == car->id) ||
            strcmp(cache->cars[i].entry_number, car->entry_number) == 0) {
            return i;
        }
    }
    return -1;
}

static cJSON *cache_to_json(const top_spot_car_cache_t *cache)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "schema_version", CAR_CACHE_SCHEMA_VERSION);
    cJSON_AddNumberToObject(root, "show_id", cache->show_id);
    cJSON_AddNumberToObject(root, "data_revision", cache->data_revision);
    cJSON *cars = cJSON_AddArrayToObject(root, "cars");
    int count = cache_safe_count(cache, "serialize");
    for (int i = 0; i < count; i++) {
        const top_spot_car_status_t *src = &cache->cars[i];
        cJSON *car = cJSON_CreateObject();
        cJSON_AddNumberToObject(car, "id", src->id);
        cJSON_AddStringToObject(car, "entry_number", src->entry_number);
        cJSON_AddStringToObject(car, "participant", src->participant);
        cJSON_AddStringToObject(car, "year", src->year);
        cJSON_AddStringToObject(car, "make", src->make);
        cJSON_AddStringToObject(car, "model", src->model);
        cJSON_AddStringToObject(car, "vehicle_type", src->vehicle_type);
        cJSON_AddStringToObject(car, "status", src->status);
        cJSON_AddNumberToObject(car, "data_revision", src->data_revision);
        cJSON_AddStringToObject(car, "judged_source", src->judged_source);
        cJSON_AddStringToObject(car, "judged_at", src->judged_at);
        cJSON_AddItemToArray(cars, car);
    }
    return root;
}

static esp_err_t load_cache(int show_id, top_spot_car_cache_t *cache)
{
    ESP_LOGI(TAG, "BOOT TRACE: load_cache entry show_id=%d", show_id);
    char path[96];
    esp_err_t err = cache_path(show_id, path, sizeof(path));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BOOT TRACE: load_cache path failed err=%s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "BOOT TRACE: load_cache path=%s", path);
    char *json = NULL;
    err = read_text_file(path, &json);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BOOT TRACE: load_cache read failed err=%s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "BOOT TRACE: load_cache before cJSON_Parse");
    cJSON *root = cJSON_Parse(json);
    ESP_LOGI(TAG, "BOOT TRACE: load_cache after cJSON_Parse root=%p", root);
    free(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "BOOT TRACE: load_cache before cache memset");
    memset(cache, 0, sizeof(*cache));
    cache->loaded = true;
    cache->show_id = json_int(root, "show_id", show_id);
    cache->data_revision = json_int(root, "data_revision", 0);
    cJSON *cars = cJSON_GetObjectItemCaseSensitive(root, "cars");
    if (cJSON_IsArray(cars)) {
        ESP_LOGI(TAG, "BOOT TRACE: load_cache before cars loop count=%d", cJSON_GetArraySize(cars));
        cJSON *car = NULL;
        cJSON_ArrayForEach(car, cars) {
            if (cache->car_count >= TOP_SPOT_MAX_CACHED_CARS) {
                break;
            }
            parse_car(&cache->cars[cache->car_count++], car);
            if ((cache->car_count % 32) == 0) {
                vTaskDelay(1);
            }
        }
        ESP_LOGI(TAG, "BOOT TRACE: load_cache after cars loop loaded=%d", cache->car_count);
    }
    ESP_LOGI(TAG, "BOOT TRACE: load_cache before cJSON_Delete");
    cJSON_Delete(root);
    err = cache->show_id == show_id ? ESP_OK : ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "BOOT TRACE: load_cache return err=%s", esp_err_to_name(err));
    return err;
}

static bool status_matches_filter(const top_spot_car_status_t *car, top_spot_car_filter_t filter)
{
    switch (filter) {
    case TOP_SPOT_CAR_FILTER_UNJUDGED:
        return strcmp(car->status, TOP_SPOT_CAR_STATUS_UNJUDGED) == 0;
    case TOP_SPOT_CAR_FILTER_JUDGED:
        return strcmp(car->status, TOP_SPOT_CAR_STATUS_JUDGED) == 0 ||
               strcmp(car->status, TOP_SPOT_CAR_STATUS_CONFLICT) == 0;
    case TOP_SPOT_CAR_FILTER_PENDING:
        return strcmp(car->status, TOP_SPOT_CAR_STATUS_PENDING_SYNC) == 0;
    case TOP_SPOT_CAR_FILTER_ALL:
    default:
        return true;
    }
}

static bool numeric_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0') {
        return false;
    }
    for (const char *p = a; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    for (const char *p = b; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    return atoi(a) == atoi(b);
}

esp_err_t top_spot_car_status_store_save(const top_spot_car_cache_t *cache)
{
    if (cache == NULL || cache->show_id <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[96];
    esp_err_t err = cache_path(cache->show_id, path, sizeof(path));
    if (err != ESP_OK) {
        return err;
    }
    cJSON *root = cache_to_json(cache);
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    err = top_spot_file_write_text_atomic(path, json, TAG);
    cJSON_free(json);
    return err;
}

esp_err_t top_spot_car_status_store_apply_sync(top_spot_app_state_t *state, cJSON *cars, int show_id, int data_revision, bool full_snapshot)
{
    if (state == NULL || show_id <= 0 || !cJSON_IsArray(cars)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ensure_mutex(), TAG, "car status mutex failed");

    int received = cJSON_GetArraySize(cars);
    log_stack_headroom("apply_sync entry");
    ESP_LOGI(TAG,
             "CAR STATUS SYNC: mode=%s received=%d max=%d sizeof(car)=%u sizeof(cache)=%u sizeof(pending)=%u",
             full_snapshot ? "FULL" : "DELTA", received, TOP_SPOT_MAX_CACHED_CARS,
             (unsigned)sizeof(top_spot_car_status_t),
             (unsigned)sizeof(top_spot_car_cache_t),
             (unsigned)sizeof(pending_entry_snapshot_t));

    pending_entry_snapshot_t *pending = car_status_alloc(sizeof(*pending));
    top_spot_car_cache_t *working = car_status_alloc(sizeof(*working));
    if (pending == NULL || working == NULL) {
        free(pending);
        free(working);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "CAR STATUS SYNC: pending alloc=%p location=%s working_cache alloc=%p location=%s",
             pending, alloc_location(pending), working, alloc_location(working));

    collect_pending_entries(pending);
    log_stack_headroom("apply_sync after pending collect");

    int existing = 0;
    lock_cache();
    s_state = state;
    s_ready = top_spot_storage_is_ready();
    existing = (state->car_cache.show_id == show_id) ? cache_safe_count(&state->car_cache, "apply_sync existing") : 0;
    if (full_snapshot || state->car_cache.show_id != show_id) {
        memset(working, 0, sizeof(*working));
        working->loaded = true;
        working->show_id = show_id;
    } else {
        *working = state->car_cache;
        working->car_count = cache_safe_count(working, "apply_sync working copy");
    }
    unlock_cache();

    working->data_revision = data_revision;

    cJSON *car_json = NULL;
    int applied = 0;
    int inserted = 0;
    int updated = 0;
    int skipped = 0;
    cJSON_ArrayForEach(car_json, cars) {
        top_spot_car_status_t car;
        parse_car(&car, car_json);
        if (car.entry_number[0] == '\0') {
            skipped++;
            continue;
        }
        int index = find_by_id_or_entry(working, &car);
        if (index >= 0) {
            working->cars[index] = car;
            updated++;
        } else if (working->car_count < TOP_SPOT_MAX_CACHED_CARS) {
            working->cars[working->car_count++] = car;
            inserted++;
        } else {
            skipped++;
            ESP_LOGW(TAG, "CAR STATUS SYNC: roster overflow; skipping entry=%s max=%d",
                     car.entry_number, TOP_SPOT_MAX_CACHED_CARS);
            continue;
        }
        applied++;
        if ((applied % 32) == 0) {
            log_stack_headroom("apply_sync inside car loop");
            vTaskDelay(1);
        }
    }
    int pending_overlaid = overlay_pending_on_cache(working, pending);
    int retained = working->car_count - inserted - updated;
    if (retained < 0) {
        retained = 0;
    }
    log_stack_headroom("apply_sync before publish");

    lock_cache();
    state->car_cache = *working;
    unlock_cache();

    esp_err_t err = top_spot_car_status_store_save(working);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "CAR STATUS SYNC: mode=%s received=%d existing=%d inserted=%d updated=%d retained=%d pending_overlaid=%d final=%d skipped=%d",
                 full_snapshot ? "FULL" : "DELTA", received, existing, inserted, updated, retained,
                 pending_overlaid, working->car_count, skipped);
        ESP_LOGI(TAG, "Cached car status show=%d revision=%d cars=%d applied=%d skipped=%d",
                 show_id, data_revision, working->car_count, applied, skipped);
    } else {
        ESP_LOGW(TAG, "Car status cache write failed: %s", esp_err_to_name(err));
    }
    log_stack_headroom("apply_sync exit");
    free(pending);
    free(working);
    return err;
}

esp_err_t top_spot_car_status_store_init(top_spot_app_state_t *state)
{
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init entry state=%p", state);
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before ensure_mutex");
    ESP_RETURN_ON_ERROR(ensure_mutex(), TAG, "car status mutex failed");
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after ensure_mutex mutex=%p", s_mutex);
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before initial lock");
    lock_cache();
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after initial lock");
    s_state = state;
    s_ready = top_spot_storage_is_ready();
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init storage_ready=%d show_id=%d",
             s_ready, state->show.show_id);
    unlock_cache();
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after initial unlock");
    if (!s_ready || state->show.show_id <= 0) {
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init early return ready=%d show_id=%d",
                 s_ready, state->show.show_id);
        return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    char path[96];
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before cache_path");
    if (cache_path(state->show.show_id, path, sizeof(path)) == ESP_OK) {
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before recover_atomic path=%s", path);
        top_spot_file_recover_atomic(path, TAG);
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after recover_atomic");
    }

    top_spot_car_cache_t *loaded = car_status_alloc(sizeof(*loaded));
    if (loaded == NULL) {
        ESP_LOGW(TAG, "BOOT TRACE: car_status_store_init loaded cache allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init loaded cache alloc=%p location=%s size=%u",
             loaded, alloc_location(loaded), (unsigned)sizeof(*loaded));
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before load_cache");
    esp_err_t err = load_cache(state->show.show_id, loaded);
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after load_cache err=%s", esp_err_to_name(err));
    if (err == ESP_OK) {
        pending_entry_snapshot_t *pending = car_status_alloc(sizeof(*pending));
        if (pending == NULL) {
            free(loaded);
            ESP_LOGW(TAG, "BOOT TRACE: car_status_store_init pending allocation failed");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init pending alloc=%p location=%s size=%u",
                 pending, alloc_location(pending), (unsigned)sizeof(*pending));
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before pending collect");
        collect_pending_entries(pending);
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before local pending overlay");
        overlay_pending_on_cache(loaded, pending);
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after local pending overlay");
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init before publish lock");
        lock_cache();
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after publish lock");
        state->car_cache = *loaded;
        unlock_cache();
        ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init after publish unlock");
        ESP_LOGI(TAG, "Loaded cached car status show=%d revision=%d cars=%d",
                 loaded->show_id, loaded->data_revision, loaded->car_count);
        free(pending);
    } else {
        ESP_LOGW(TAG, "No cached car status loaded: %s", esp_err_to_name(err));
    }
    free(loaded);
    ESP_LOGI(TAG, "BOOT TRACE: car_status_store_init return ESP_OK");
    return ESP_OK;
}

bool top_spot_car_status_store_is_ready(void)
{
    bool ready;
    lock_cache();
    ready = s_ready;
    unlock_cache();
    return ready;
}

int top_spot_car_status_count(void)
{
    int count = 0;
    lock_cache();
    if (s_state != NULL) {
        count = cache_safe_count(&s_state->car_cache, "count");
    }
    unlock_cache();
    return count;
}

int top_spot_car_status_count_by_state(const char *status)
{
    int count = 0;
    lock_cache();
    if (s_state != NULL) {
        int car_count = cache_safe_count(&s_state->car_cache, "count_by_state");
        for (int i = 0; i < car_count; i++) {
            if (strcmp(s_state->car_cache.cars[i].status, status) == 0) {
                count++;
            }
        }
    }
    unlock_cache();
    return count;
}

int top_spot_car_status_visible_count(top_spot_car_filter_t filter)
{
    int count = 0;
    lock_cache();
    if (s_state != NULL) {
        int car_count = cache_safe_count(&s_state->car_cache, "visible_count");
        for (int i = 0; i < car_count; i++) {
            if (status_matches_filter(&s_state->car_cache.cars[i], filter)) {
                count++;
            }
        }
    }
    unlock_cache();
    return count;
}

bool top_spot_car_status_get_visible(top_spot_car_filter_t filter, int visible_index, top_spot_car_status_t *out_car)
{
    bool found = false;
    int seen = 0;
    lock_cache();
    if (s_state != NULL && out_car != NULL) {
        int car_count = cache_safe_count(&s_state->car_cache, "get_visible");
        for (int i = 0; i < car_count; i++) {
            if (!status_matches_filter(&s_state->car_cache.cars[i], filter)) {
                continue;
            }
            if (seen == visible_index) {
                *out_car = s_state->car_cache.cars[i];
                found = true;
                break;
            }
            seen++;
        }
    }
    unlock_cache();
    return found;
}

bool top_spot_car_status_find_entry(const char *entry, top_spot_car_status_t *out_car)
{
    bool found = false;
    lock_cache();
    if (s_state != NULL && entry != NULL) {
        int car_count = cache_safe_count(&s_state->car_cache, "find_entry");
        for (int i = 0; i < car_count; i++) {
            top_spot_car_status_t *car = &s_state->car_cache.cars[i];
            if (strcmp(car->entry_number, entry) == 0 || numeric_equal(car->entry_number, entry)) {
                if (out_car != NULL) {
                    *out_car = *car;
                }
                found = true;
                break;
            }
        }
    }
    unlock_cache();
    return found;
}

bool top_spot_car_status_entry_is_pending(const char *entry)
{
    for (size_t i = 0;; i++) {
        const top_spot_record_t *record = top_spot_record_store_get(i);
        if (record == NULL) {
            break;
        }
        if (strcmp(record->entry_number, entry) == 0 &&
            strcmp(record->sync_state, TOP_SPOT_SYNC_PENDING) == 0) {
            return true;
        }
    }
    return false;
}

static void collect_pending_entries(pending_entry_snapshot_t *pending)
{
    memset(pending, 0, sizeof(*pending));
    int record_count = top_spot_record_store_count();
    if (record_count > TOP_SPOT_MAX_SAVED_RECORDS) {
        record_count = TOP_SPOT_MAX_SAVED_RECORDS;
    }
    ESP_LOGI(TAG, "BOOT TRACE: already-judged collect pending start records=%d", record_count);
    for (int i = 0; i < record_count; i++) {
        const top_spot_record_t *record = top_spot_record_store_get((size_t)i);
        if (record == NULL) {
            continue;
        }
        if (strcmp(record->sync_state, TOP_SPOT_SYNC_PENDING) == 0 && record->entry_number[0] != '\0') {
            copy_text(pending->entries[pending->count], sizeof(pending->entries[pending->count]), record->entry_number);
            pending->count++;
        }
        if ((i % 8) == 7) {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "BOOT TRACE: already-judged collect pending done pending=%d", pending->count);
}

static bool entry_in_pending_snapshot(const pending_entry_snapshot_t *pending, const char *entry)
{
    for (int i = 0; i < pending->count; i++) {
        if (strcmp(pending->entries[i], entry) == 0 || numeric_equal(pending->entries[i], entry)) {
            return true;
        }
    }
    return false;
}

static int overlay_pending_on_cache(top_spot_car_cache_t *cache, const pending_entry_snapshot_t *pending)
{
    if (cache == NULL || pending == NULL) {
        return 0;
    }
    ESP_LOGI(TAG, "BOOT TRACE: already-judged overlay start cars=%d pending=%d",
             cache->car_count, pending->count);
    int car_count = cache_safe_count(cache, "overlay_pending");
    int overlaid = 0;
    for (int i = 0; i < car_count; i++) {
        top_spot_car_status_t *car = &cache->cars[i];
        if (strcmp(car->status, TOP_SPOT_CAR_STATUS_JUDGED) != 0 &&
            entry_in_pending_snapshot(pending, car->entry_number)) {
            copy_text(car->status, sizeof(car->status), TOP_SPOT_CAR_STATUS_PENDING_SYNC);
            copy_text(car->judged_source, sizeof(car->judged_source), "this_handheld");
            overlaid++;
        }
        if ((i % 32) == 31) {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "BOOT TRACE: already-judged overlay done cars=%d overlaid=%d", cache->car_count, overlaid);
    return overlaid;
}

void top_spot_car_status_overlay_pending(void)
{
    if (s_state == NULL) {
        return;
    }
    pending_entry_snapshot_t pending;
    collect_pending_entries(&pending);
    lock_cache();
    overlay_pending_on_cache(&s_state->car_cache, &pending);
    unlock_cache();
}
