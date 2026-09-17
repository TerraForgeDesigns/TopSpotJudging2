#include "p4_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "top_spot_storage";
static const char *SELF_TEST_TEXT = "Top Spot storage self-test PASS\n";

static SemaphoreHandle_t s_storage_mutex;
static bool s_init_attempted;
static bool s_mounted;
static bool s_self_test_passed;
static esp_err_t s_last_error = ESP_ERR_INVALID_STATE;

static esp_err_t ensure_mutex(void)
{
    if (s_storage_mutex == NULL) {
        s_storage_mutex = xSemaphoreCreateMutex();
        if (s_storage_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void lock_storage(void)
{
    if (s_storage_mutex != NULL) {
        xSemaphoreTake(s_storage_mutex, portMAX_DELAY);
    }
}

static void unlock_storage(void)
{
    if (s_storage_mutex != NULL) {
        xSemaphoreGive(s_storage_mutex);
    }
}

static const char *card_type(void)
{
    if (bsp_sdcard == NULL) {
        return "unknown";
    }
    if (bsp_sdcard->is_sdio) {
        return "SDIO";
    }
    if (bsp_sdcard->is_mmc) {
        return "MMC";
    }
    return (bsp_sdcard->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";
}

static uint64_t card_capacity_bytes(void)
{
    if (bsp_sdcard == NULL) {
        return 0;
    }
    return (uint64_t)bsp_sdcard->csd.capacity * (uint64_t)bsp_sdcard->csd.sector_size;
}

static esp_err_t mkdir_if_needed(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "%s exists but is not a directory", path);
        return ESP_ERR_INVALID_STATE;
    }

    if (mkdir(path, 0775) == 0) {
        ESP_LOGI(TAG, "Created directory %s", path);
        return ESP_OK;
    }

    if (errno == EEXIST) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to create %s: errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t ensure_directory_tree(void)
{
    const char *dirs[] = {
        BSP_SD_MOUNT_POINT "/topspot",
        BSP_SD_MOUNT_POINT "/topspot/shows",
        BSP_SD_MOUNT_POINT "/topspot/photos",
        BSP_SD_MOUNT_POINT "/topspot/photos/vehicle",
        BSP_SD_MOUNT_POINT "/topspot/photos/judge_sheets",
        BSP_SD_MOUNT_POINT "/topspot/records",
        BSP_SD_MOUNT_POINT "/topspot/system",
    };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        esp_err_t err = mkdir_if_needed(dirs[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t run_self_test(void)
{
    const char *path = BSP_SD_MOUNT_POINT "/topspot/system/storage_test.txt";
    char read_buf[64] = {0};

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Self-test open for write failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t expected_len = strlen(SELF_TEST_TEXT);
    size_t written = fwrite(SELF_TEST_TEXT, 1, expected_len, file);
    if (written != expected_len) {
        ESP_LOGE(TAG, "Self-test write failed: wrote %u of %u bytes", (unsigned)written, (unsigned)expected_len);
        fclose(file);
        return ESP_FAIL;
    }

    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Self-test close after write failed: errno=%d", errno);
        return ESP_FAIL;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Self-test open for read failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t read_len = fread(read_buf, 1, sizeof(read_buf) - 1, file);
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Self-test close after read failed: errno=%d", errno);
        return ESP_FAIL;
    }

    if (read_len != expected_len || memcmp(read_buf, SELF_TEST_TEXT, expected_len) != 0) {
        ESP_LOGE(TAG, "Self-test verify failed: expected %u bytes, read %u bytes",
                 (unsigned)expected_len, (unsigned)read_len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Storage self-test PASS: %s", path);
    return ESP_OK;
}

esp_err_t top_spot_storage_init(void)
{
    esp_err_t err = ensure_mutex();
    if (err != ESP_OK) {
        s_last_error = err;
        return err;
    }

    lock_storage();
    if (s_init_attempted) {
        err = s_last_error;
        unlock_storage();
        return err;
    }
    s_init_attempted = true;

    ESP_LOGI(TAG, "Initializing microSD storage at %s", BSP_SD_MOUNT_POINT);
    err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "microSD mount failed: %s", esp_err_to_name(err));
        s_last_error = err;
        unlock_storage();
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "microSD mounted: type=%s name=%s capacity=%llu MB sector=%d bytes",
             card_type(),
             bsp_sdcard != NULL ? bsp_sdcard->cid.name : "unknown",
             card_capacity_bytes() / (1024ULL * 1024ULL),
             bsp_sdcard != NULL ? bsp_sdcard->csd.sector_size : 0);
    sdmmc_card_print_info(stdout, bsp_sdcard);

    err = ensure_directory_tree();
    if (err == ESP_OK) {
        err = run_self_test();
    }

    s_self_test_passed = (err == ESP_OK);
    s_last_error = err;
    if (err == ESP_OK) {
        uint64_t total = 0;
        uint64_t free = 0;
        if (esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &free) == ESP_OK) {
            ESP_LOGI(TAG, "Storage ready: total=%llu MB free=%llu MB",
                     total / (1024ULL * 1024ULL), free / (1024ULL * 1024ULL));
        } else {
            ESP_LOGW(TAG, "Storage ready, but free-space query failed");
        }
    } else {
        ESP_LOGE(TAG, "Storage self-test/setup failed: %s", esp_err_to_name(err));
    }

    unlock_storage();
    return err;
}

bool top_spot_storage_is_ready(void)
{
    bool ready;
    lock_storage();
    ready = s_mounted && s_self_test_passed;
    unlock_storage();
    return ready;
}

uint64_t top_spot_storage_get_total_space(void)
{
    uint64_t total = 0;
    lock_storage();
    if (s_mounted) {
        esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, NULL);
    }
    unlock_storage();
    return total;
}

uint64_t top_spot_storage_get_free_space(void)
{
    uint64_t free = 0;
    lock_storage();
    if (s_mounted) {
        esp_vfs_fat_info(BSP_SD_MOUNT_POINT, NULL, &free);
    }
    unlock_storage();
    return free;
}

const char *top_spot_storage_mount_point(void)
{
    return BSP_SD_MOUNT_POINT;
}

const char *top_spot_storage_status_text(void)
{
    return top_spot_storage_is_ready() ? "Storage Ready" : "Storage Problem";
}
