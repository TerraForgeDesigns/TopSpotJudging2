#include "p4_file_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static bool exists_at(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static esp_err_t make_sidecar_path(const char *final_path, const char *suffix, char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s%s", final_path, suffix);
    if (written < 0 || (size_t)written >= out_size) {
        return ESP_ERR_INVALID_SIZE;
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
    if (size < 0 || size > 65536) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(file);

    char *text = heap_caps_calloc(1, (size_t)size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (text == NULL && size < 1024) {
        text = heap_caps_calloc(1, (size_t)size + 1, MALLOC_CAP_8BIT);
    }
    if (text == NULL) {
        ESP_LOGE("top_spot_file_store",
                 "Unable to allocate verify buffer size=%ld psram_largest=%u internal_largest=%u dma_largest=%u",
                 size + 1,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
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

static esp_err_t verify_text_file(const char *path, const char *expected, const char *log_tag)
{
    char *verify = NULL;
    esp_err_t err = read_text_file(path, &verify);
    if (err != ESP_OK || verify == NULL || strcmp(verify, expected) != 0) {
        ESP_LOGE(log_tag, "Atomic update: temp verification failed for %s", path);
        free(verify);
        return ESP_FAIL;
    }
    free(verify);
    ESP_LOGI(log_tag, "Atomic update: temp verified %s", path);
    return ESP_OK;
}

esp_err_t top_spot_file_recover_atomic(const char *final_path, const char *log_tag)
{
    if (final_path == NULL || log_tag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char tmp_path[256];
    char bak_path[256];
    esp_err_t err = make_sidecar_path(final_path, ".tmp", tmp_path, sizeof(tmp_path));
    if (err != ESP_OK) {
        return err;
    }
    err = make_sidecar_path(final_path, ".bak", bak_path, sizeof(bak_path));
    if (err != ESP_OK) {
        return err;
    }

    bool final_exists = exists_at(final_path);
    bool tmp_exists = exists_at(tmp_path);
    bool bak_exists = exists_at(bak_path);

    if (final_exists) {
        if (tmp_exists && unlink(tmp_path) == 0) {
            ESP_LOGW(log_tag, "Atomic recovery: removed stale temp %s", tmp_path);
        }
        if (bak_exists && unlink(bak_path) == 0) {
            ESP_LOGW(log_tag, "Atomic recovery: removed stale backup %s", bak_path);
        }
        return ESP_OK;
    }

    if (bak_exists) {
        if (rename(bak_path, final_path) != 0) {
            ESP_LOGE(log_tag, "Atomic recovery: failed to restore %s from %s errno=%d",
                     final_path, bak_path, errno);
            return ESP_FAIL;
        }
        ESP_LOGW(log_tag, "Atomic recovery: restored %s from backup", final_path);
        if (tmp_exists && unlink(tmp_path) == 0) {
            ESP_LOGW(log_tag, "Atomic recovery: removed stale temp %s", tmp_path);
        }
        return ESP_OK;
    }

    if (tmp_exists) {
        if (rename(tmp_path, final_path) != 0) {
            ESP_LOGE(log_tag, "Atomic recovery: failed to promote orphan temp %s to %s errno=%d",
                     tmp_path, final_path, errno);
            return ESP_FAIL;
        }
        ESP_LOGW(log_tag, "Atomic recovery: promoted orphan temp %s", final_path);
    }
    return ESP_OK;
}

esp_err_t top_spot_file_write_text_atomic(const char *final_path, const char *text, const char *log_tag)
{
    if (final_path == NULL || text == NULL || log_tag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char tmp_path[256];
    char bak_path[256];
    esp_err_t err = make_sidecar_path(final_path, ".tmp", tmp_path, sizeof(tmp_path));
    if (err != ESP_OK) {
        return err;
    }
    err = make_sidecar_path(final_path, ".bak", bak_path, sizeof(bak_path));
    if (err != ESP_OK) {
        return err;
    }

    (void)top_spot_file_recover_atomic(final_path, log_tag);
    unlink(tmp_path);

    FILE *file = fopen(tmp_path, "w");
    if (file == NULL) {
        ESP_LOGE(log_tag, "Atomic update: failed to open temp %s errno=%d", tmp_path, errno);
        return ESP_FAIL;
    }

    size_t len = strlen(text);
    if (fwrite(text, 1, len, file) != len) {
        ESP_LOGE(log_tag, "Atomic update: failed writing temp %s", tmp_path);
        fclose(file);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    if (fflush(file) != 0) {
        ESP_LOGE(log_tag, "Atomic update: failed flushing temp %s errno=%d", tmp_path, errno);
        fclose(file);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    if (fsync(fileno(file)) != 0) {
        ESP_LOGW(log_tag, "Atomic update: fsync failed for temp %s errno=%d", tmp_path, errno);
    }
    if (fclose(file) != 0) {
        ESP_LOGE(log_tag, "Atomic update: failed closing temp %s errno=%d", tmp_path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    err = verify_text_file(tmp_path, text, log_tag);
    if (err != ESP_OK) {
        unlink(tmp_path);
        return err;
    }

    bool had_final = exists_at(final_path);
    if (had_final) {
        unlink(bak_path);
        if (rename(final_path, bak_path) != 0) {
            ESP_LOGE(log_tag, "Atomic update: failed moving existing final %s to backup %s errno=%d",
                     final_path, bak_path, errno);
            unlink(tmp_path);
            return ESP_FAIL;
        }
        ESP_LOGI(log_tag, "Atomic update: existing final moved to backup %s", bak_path);
    }

    if (rename(tmp_path, final_path) != 0) {
        ESP_LOGE(log_tag, "Atomic update: failed promoting temp %s to %s errno=%d",
                 tmp_path, final_path, errno);
        if (had_final && !exists_at(final_path) && exists_at(bak_path)) {
            if (rename(bak_path, final_path) == 0) {
                ESP_LOGW(log_tag, "Atomic update: restored backup after promotion failure");
            } else {
                ESP_LOGE(log_tag, "Atomic update: failed restoring backup %s errno=%d",
                         bak_path, errno);
            }
        }
        return ESP_FAIL;
    }
    ESP_LOGI(log_tag, "Atomic update: temp promoted to %s", final_path);

    if (had_final && unlink(bak_path) == 0) {
        ESP_LOGI(log_tag, "Atomic update: backup removed %s", bak_path);
    }
    return ESP_OK;
}
