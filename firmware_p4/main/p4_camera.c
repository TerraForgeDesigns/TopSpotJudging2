#include "p4_camera.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "driver/jpeg_encode.h"
#include "esp_cache.h"
#include "esp_cam_sensor_types.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "p4_storage.h"
#include "sdkconfig.h"

static const char *TAG = "top_spot_camera";

#define CAMERA_DEVICE_PATH ESP_VIDEO_MIPI_CSI_DEVICE_NAME
#define CAMERA_SCCB_PORT 0
#define CAMERA_SCCB_SCL 8
#define CAMERA_SCCB_SDA 7
#define CAMERA_SCCB_FREQ_HZ (10 * 1000)
#define CAMERA_RESET_PIN (-1)
#define CAMERA_PWDN_PIN (-1)
#define CAMERA_BUFFER_COUNT 2
#define CAMERA_PREVIEW_TASK_STACK (6 * 1024)
#define CAMERA_PREVIEW_TASK_PRIORITY 4
#define CAMERA_JPEG_QUALITY 84
#define CAMERA_JPEG_TIMEOUT_MS 250

typedef struct {
    bool init_attempted;
    bool ready;
    bool streaming;
    int fd;
    uint32_t width;
    uint32_t height;
    size_t frame_size;
    size_t cache_align;
    uint8_t *driver_buffers[CAMERA_BUFFER_COUNT];
    size_t driver_buffer_lengths[CAMERA_BUFFER_COUNT];
    uint8_t *latest_frame;
    uint8_t *capture_frame;
    lv_obj_t *preview_canvas;
    TaskHandle_t preview_task;
    SemaphoreHandle_t mutex;
    uint32_t capture_sequence;
    uint32_t buffer_log_count;
    uint32_t preview_session_id;
    uint32_t fresh_frame_session_id;
    bool buffers_queued;
    bool fresh_frame_ready;
    esp_err_t last_error;
} top_spot_camera_state_t;

static top_spot_camera_state_t s_camera = {
    .fd = -1,
    .last_error = ESP_ERR_INVALID_STATE,
};

static esp_err_t ensure_mutex(void)
{
    if (s_camera.mutex == NULL) {
        s_camera.mutex = xSemaphoreCreateMutex();
        if (s_camera.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void lock_camera(void)
{
    if (s_camera.mutex != NULL) {
        xSemaphoreTake(s_camera.mutex, portMAX_DELAY);
    }
}

static void unlock_camera(void)
{
    if (s_camera.mutex != NULL) {
        xSemaphoreGive(s_camera.mutex);
    }
}

static const char *bayer_pattern_name(esp_cam_sensor_bayer_pattern_t pattern)
{
    switch (pattern) {
    case ESP_CAM_SENSOR_BAYER_RGGB:
        return "RGGB";
    case ESP_CAM_SENSOR_BAYER_GRBG:
        return "GRBG";
    case ESP_CAM_SENSOR_BAYER_GBRG:
        return "GBRG";
    case ESP_CAM_SENSOR_BAYER_BGGR:
        return "BGGR";
    case ESP_CAM_SENSOR_BAYER_MONO:
        return "MONO";
    default:
        return "unknown";
    }
}

static esp_cam_sensor_bayer_pattern_t bayer_pattern_after_180(esp_cam_sensor_bayer_pattern_t pattern)
{
    switch (pattern) {
    case ESP_CAM_SENSOR_BAYER_RGGB:
        return ESP_CAM_SENSOR_BAYER_BGGR;
    case ESP_CAM_SENSOR_BAYER_BGGR:
        return ESP_CAM_SENSOR_BAYER_RGGB;
    case ESP_CAM_SENSOR_BAYER_GBRG:
        return ESP_CAM_SENSOR_BAYER_GRBG;
    case ESP_CAM_SENSOR_BAYER_GRBG:
        return ESP_CAM_SENSOR_BAYER_GBRG;
    default:
        return pattern;
    }
}

static void log_sensor_format_diagnostics(int fd)
{
    esp_cam_sensor_format_t sensor_format = {0};
    if (ioctl(fd, VIDIOC_G_SENSOR_FMT, &sensor_format) != 0) {
        ESP_LOGW(TAG, "VIDIOC_G_SENSOR_FMT failed while logging camera diagnostics: errno=%d", errno);
        return;
    }

    esp_cam_sensor_bayer_pattern_t bayer = ESP_CAM_SENSOR_BAYER_MONO;
    if (sensor_format.isp_info != NULL) {
        bayer = sensor_format.isp_info->isp_v1_info.bayer_type;
    }

    ESP_LOGI(TAG, "OV5647 sensor format: %s %lux%lu fps=%lu raw_format=%lu",
             sensor_format.name != NULL ? sensor_format.name : "(unnamed)",
             (unsigned long)sensor_format.width,
             (unsigned long)sensor_format.height,
             (unsigned long)sensor_format.fps,
             (unsigned long)sensor_format.format);
    ESP_LOGI(TAG, "OV5647 RAW Bayer pattern reported to ESP32-P4 ISP: %s",
             bayer_pattern_name(bayer));
    ESP_LOGI(TAG, "Sensor-level HFLIP/VFLIP disabled; ISP keeps unflipped %s Bayer phase",
             bayer_pattern_name(bayer));
    ESP_LOGI(TAG, "If both sensor flips were applied to even-sized %lux%lu %s RAW, effective Bayer phase would become %s",
             (unsigned long)sensor_format.width,
             (unsigned long)sensor_format.height,
             bayer_pattern_name(bayer),
             bayer_pattern_name(bayer_pattern_after_180(bayer)));
    ESP_LOGI(TAG, "Production 180 degree orientation is applied after demosaic by rotating RGB565 frames");
}

static void rotate_rgb565_180(const uint8_t *src, uint8_t *dst, size_t pixel_count)
{
    const uint16_t *src_pixels = (const uint16_t *)src;
    uint16_t *dst_pixels = (uint16_t *)dst;

    for (size_t i = 0; i < pixel_count; i++) {
        dst_pixels[pixel_count - 1 - i] = src_pixels[i];
    }
}

static esp_err_t configure_video_device(void)
{
    struct v4l2_capability capability = {0};
    if (ioctl(s_camera.fd, VIDIOC_QUERYCAP, &capability) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed: errno=%d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Camera driver=%s card=%s bus=%s version=%u.%u.%u",
             capability.driver,
             capability.card,
             capability.bus_info,
             (uint16_t)(capability.version >> 16),
             (uint8_t)(capability.version >> 8),
             (uint8_t)capability.version);

    struct v4l2_format format = {0};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = TOP_SPOT_CAMERA_WIDTH;
    format.fmt.pix.height = TOP_SPOT_CAMERA_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;

    if (ioctl(s_camera.fd, VIDIOC_S_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT RGB565 %dx%d failed: errno=%d",
                 TOP_SPOT_CAMERA_WIDTH, TOP_SPOT_CAMERA_HEIGHT, errno);
        return ESP_FAIL;
    }

    if (ioctl(s_camera.fd, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed: errno=%d", errno);
        return ESP_FAIL;
    }

    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        ESP_LOGE(TAG, "Camera did not accept RGB565 format");
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_camera.width = format.fmt.pix.width;
    s_camera.height = format.fmt.pix.height;
    s_camera.frame_size = s_camera.width * s_camera.height * 2;
    ESP_LOGI(TAG, "Camera format: RGB565 %lux%lu frame=%u bytes",
             (unsigned long)s_camera.width,
             (unsigned long)s_camera.height,
             (unsigned)s_camera.frame_size);
    ESP_LOGI(TAG, "Camera sensor path: OV5647 RAW8 800x640, ISP output=RGB565");
    log_sensor_format_diagnostics(s_camera.fd);
#if CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER
    ESP_LOGI(TAG, "ISP tuning active: esp_video ISP pipeline controller enabled");
#if CONFIG_CAMERA_OV5647_DEFAULT_IPA_JSON_CONFIGURATION_FILE
    ESP_LOGI(TAG, "ISP tuning profile: OV5647 default IPA JSON (AWB/AE/BF/gamma/CCM/saturation/contrast)");
#elif CONFIG_CAMERA_OV5647_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE
    ESP_LOGI(TAG, "ISP tuning profile: OV5647 custom IPA JSON");
#else
    ESP_LOGW(TAG, "ISP tuning profile: no OV5647 IPA JSON selected");
#endif
#else
    ESP_LOGW(TAG, "ISP tuning inactive: pipeline controller disabled");
#endif
    return ESP_OK;
}

static esp_err_t allocate_buffers(void)
{
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_camera.cache_align);
    if (err != ESP_OK || s_camera.cache_align == 0) {
        s_camera.cache_align = 128;
    }

    s_camera.latest_frame = heap_caps_aligned_alloc(
        s_camera.cache_align, s_camera.frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_camera.capture_frame = heap_caps_aligned_alloc(
        s_camera.cache_align, s_camera.frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_camera.latest_frame == NULL || s_camera.capture_frame == NULL) {
        ESP_LOGE(TAG, "Failed to allocate preview/capture frame buffers");
        return ESP_ERR_NO_MEM;
    }

    memset(s_camera.latest_frame, 0, s_camera.frame_size);
    memset(s_camera.capture_frame, 0, s_camera.frame_size);
    return ESP_OK;
}

static bool should_log_buffer_detail(void)
{
    if (s_camera.buffer_log_count < 16) {
        s_camera.buffer_log_count++;
        return true;
    }
    return false;
}

static void log_buffer_detail(const char *op, const struct v4l2_buffer *buf, int rc, int err)
{
    if (rc == 0 && !should_log_buffer_detail()) {
        return;
    }

    ESP_LOGI(TAG, "%s rc=%d errno=%d index=%lu type=%lu memory=%lu length=%lu bytesused=%lu userptr=0x%lx",
             op,
             rc,
             rc == 0 ? 0 : err,
             (unsigned long)buf->index,
             (unsigned long)buf->type,
             (unsigned long)buf->memory,
             (unsigned long)buf->length,
             (unsigned long)buf->bytesused,
             (unsigned long)buf->m.userptr);
}

static esp_err_t queue_capture_buffers(void)
{
    struct v4l2_requestbuffers req = {
        .count = CAMERA_BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
    };

    if (ioctl(s_camera.fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: errno=%d", errno);
        return ESP_FAIL;
    }

    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_USERPTR,
            .index = i,
        };

        int rc = ioctl(s_camera.fd, VIDIOC_QUERYBUF, &buf);
        log_buffer_detail("VIDIOC_QUERYBUF init", &buf, rc, errno);
        if (rc != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF buffer %d failed: errno=%d", i, errno);
            return ESP_FAIL;
        }

        s_camera.driver_buffers[i] = heap_caps_aligned_alloc(
            s_camera.cache_align, buf.length, MALLOC_CAP_SPIRAM | MALLOC_CAP_CACHE_ALIGNED);
        if (s_camera.driver_buffers[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate camera driver buffer %d length=%lu",
                     i, (unsigned long)buf.length);
            return ESP_ERR_NO_MEM;
        }
        s_camera.driver_buffer_lengths[i] = buf.length;

        buf.m.userptr = (unsigned long)s_camera.driver_buffers[i];
        rc = ioctl(s_camera.fd, VIDIOC_QBUF, &buf);
        log_buffer_detail("VIDIOC_QBUF init", &buf, rc, errno);
        if (rc != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF buffer %d failed: errno=%d", i, errno);
            return ESP_FAIL;
        }
    }

    s_camera.buffers_queued = true;
    ESP_LOGI(TAG, "Preview buffers queued for initial session");
    return ESP_OK;
}

static esp_err_t requeue_capture_buffers(void)
{
    ESP_LOGI(TAG, "Requeueing preview buffers");
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        if (s_camera.driver_buffers[i] == NULL || s_camera.driver_buffer_lengths[i] == 0) {
            ESP_LOGE(TAG, "Cannot requeue missing camera buffer %d", i);
            return ESP_ERR_INVALID_STATE;
        }

        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_USERPTR,
            .index = i,
            .length = s_camera.driver_buffer_lengths[i],
            .m.userptr = (unsigned long)s_camera.driver_buffers[i],
        };

        int rc = ioctl(s_camera.fd, VIDIOC_QBUF, &buf);
        log_buffer_detail("VIDIOC_QBUF restart", &buf, rc, errno);
        if (rc != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF restart buffer %d failed: errno=%d", i, errno);
            return ESP_FAIL;
        }
    }

    s_camera.buffers_queued = true;
    ESP_LOGI(TAG, "Preview buffers requeued");
    return ESP_OK;
}

static void free_buffers(void)
{
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        if (s_camera.driver_buffers[i] != NULL) {
            heap_caps_free(s_camera.driver_buffers[i]);
            s_camera.driver_buffers[i] = NULL;
        }
        s_camera.driver_buffer_lengths[i] = 0;
    }
    s_camera.buffers_queued = false;
    if (s_camera.latest_frame != NULL) {
        heap_caps_free(s_camera.latest_frame);
        s_camera.latest_frame = NULL;
    }
    if (s_camera.capture_frame != NULL) {
        heap_caps_free(s_camera.capture_frame);
        s_camera.capture_frame = NULL;
    }
}

static esp_err_t set_streaming(bool enable)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    unsigned long request = enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF;
    if (ioctl(s_camera.fd, request, &type) != 0) {
        ESP_LOGE(TAG, "%s failed: errno=%d", enable ? "VIDIOC_STREAMON" : "VIDIOC_STREAMOFF", errno);
        return ESP_FAIL;
    }
    s_camera.streaming = enable;
    ESP_LOGI(TAG, "Camera stream %s", enable ? "started" : "stopped");
    return ESP_OK;
}

static void preview_task(void *arg)
{
    uint32_t session_id = (uint32_t)(uintptr_t)arg;
    ESP_LOGI(TAG, "Preview worker started for session %lu", (unsigned long)session_id);

    while (true) {
        lock_camera();
        bool streaming = s_camera.streaming;
        int fd = s_camera.fd;
        lv_obj_t *canvas = s_camera.preview_canvas;
        unlock_camera();

        if (!streaming || fd < 0) {
            break;
        }

        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_USERPTR,
        };

        int rc = ioctl(fd, VIDIOC_DQBUF, &buf);
        log_buffer_detail("VIDIOC_DQBUF preview", &buf, rc, errno);
        if (rc != 0) {
            if (errno != EAGAIN) {
                ESP_LOGW(TAG, "VIDIOC_DQBUF failed during preview: errno=%d", errno);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (buf.index >= CAMERA_BUFFER_COUNT || s_camera.driver_buffers[buf.index] == NULL) {
            ESP_LOGW(TAG, "VIDIOC_DQBUF returned invalid buffer index=%lu", (unsigned long)buf.index);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint8_t *frame = s_camera.driver_buffers[buf.index];
        if (buf.bytesused > 0) {
            size_t copy_len = buf.bytesused < s_camera.frame_size ? buf.bytesused : s_camera.frame_size;
            lock_camera();
            if (copy_len == s_camera.frame_size) {
                rotate_rgb565_180(frame, s_camera.latest_frame, s_camera.width * s_camera.height);
                memcpy(s_camera.capture_frame, s_camera.latest_frame, s_camera.frame_size);
            } else {
                memcpy(s_camera.latest_frame, frame, copy_len);
                memcpy(s_camera.capture_frame, frame, copy_len);
                memset(s_camera.latest_frame + copy_len, 0, s_camera.frame_size - copy_len);
                memset(s_camera.capture_frame + copy_len, 0, s_camera.frame_size - copy_len);
            }
            bool first_fresh_frame = !s_camera.fresh_frame_ready ||
                                     s_camera.fresh_frame_session_id != session_id;
            s_camera.fresh_frame_ready = true;
            s_camera.fresh_frame_session_id = session_id;
            unlock_camera();

            if (first_fresh_frame) {
                ESP_LOGI(TAG, "First fresh frame received for preview session %lu",
                         (unsigned long)session_id);
            }

            if (canvas != NULL && bsp_display_lock(50)) {
                lv_canvas_set_buffer(canvas, s_camera.latest_frame,
                                     s_camera.width, s_camera.height,
                                     LV_COLOR_FORMAT_RGB565);
                lv_obj_invalidate(canvas);
                bsp_display_unlock();
            }
        }

        buf.m.userptr = (unsigned long)s_camera.driver_buffers[buf.index];
        buf.length = s_camera.driver_buffer_lengths[buf.index];
        rc = ioctl(fd, VIDIOC_QBUF, &buf);
        log_buffer_detail("VIDIOC_QBUF preview", &buf, rc, errno);
        if (rc != 0) {
            ESP_LOGW(TAG, "VIDIOC_QBUF failed during preview: errno=%d", errno);
        }

        vTaskDelay(pdMS_TO_TICKS(35));
    }

    lock_camera();
    s_camera.preview_task = NULL;
    unlock_camera();
    ESP_LOGI(TAG, "Preview worker stopped for session %lu", (unsigned long)session_id);
    vTaskDelete(NULL);
}

static void sanitize_entry_number(const char *entry_number, char *out, size_t out_size)
{
    size_t pos = 0;
    const char *src = (entry_number != NULL && entry_number[0] != '\0') ? entry_number : "unknown";
    for (size_t i = 0; src[i] != '\0' && pos + 1 < out_size; i++) {
        char c = src[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            out[pos++] = c;
        } else if (c == '-' || c == '_') {
            out[pos++] = c;
        }
    }
    if (pos == 0 && out_size > 1) {
        out[pos++] = '0';
    }
    out[pos] = '\0';
}

static esp_err_t make_unique_path(top_spot_photo_type_t type, const char *entry_number,
                                  char *out_path, size_t out_path_size)
{
    const char *folder = type == TOP_SPOT_PHOTO_VEHICLE
                             ? "/sdcard/topspot/photos/vehicle"
                             : "/sdcard/topspot/photos/judge_sheets";
    const char *prefix = type == TOP_SPOT_PHOTO_VEHICLE ? "vehicle" : "judge_sheet";
    char entry[24];
    sanitize_entry_number(entry_number, entry, sizeof(entry));

    for (int attempt = 0; attempt < 100; attempt++) {
        uint32_t seq = ++s_camera.capture_sequence;
        int written = snprintf(out_path, out_path_size, "%s/%s_%s_%08lu_%02d.jpg",
                               folder, prefix, entry, (unsigned long)seq, attempt);
        if (written < 0 || (size_t)written >= out_path_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        struct stat st;
        if (stat(out_path, &st) != 0) {
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t verify_file(const char *path, size_t *out_file_size)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "Saved JPEG verify stat failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }
    if (st.st_size <= 0) {
        ESP_LOGE(TAG, "Saved JPEG verify failed, file is empty: %s", path);
        return ESP_FAIL;
    }
    if (out_file_size != NULL) {
        *out_file_size = (size_t)st.st_size;
    }
    ESP_LOGI(TAG, "Saved JPEG: %s (%u bytes)", path, (unsigned)st.st_size);
    return ESP_OK;
}

esp_err_t top_spot_camera_init(void)
{
    esp_err_t err = ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    lock_camera();
    if (s_camera.init_attempted) {
        err = s_camera.last_error;
        unlock_camera();
        return err;
    }
    s_camera.init_attempted = true;
    unlock_camera();

    ESP_LOGI(TAG, "Initializing OV5647 MIPI-CSI camera through esp_video");
    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_get_handle();
    esp_video_init_csi_config_t csi_config[] = {
        {
            .sccb_config = {
                .init_sccb = true,
                .i2c_config = {
                    .port = CAMERA_SCCB_PORT,
                    .scl_pin = CAMERA_SCCB_SCL,
                    .sda_pin = CAMERA_SCCB_SDA,
                },
                .freq = CAMERA_SCCB_FREQ_HZ,
            },
            .reset_pin = CAMERA_RESET_PIN,
            .pwdn_pin = CAMERA_PWDN_PIN,
        },
    };
    if (i2c_bus_handle != NULL) {
        csi_config[0].sccb_config.init_sccb = false;
        csi_config[0].sccb_config.i2c_handle = i2c_bus_handle;
    }
    esp_video_init_config_t video_config = {
        .csi = csi_config,
    };

    err = esp_video_init(&video_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(err));
        goto fail;
    }

    s_camera.fd = open(CAMERA_DEVICE_PATH, O_RDONLY | O_NONBLOCK, 0);
    if (s_camera.fd < 0) {
        ESP_LOGE(TAG, "Open %s failed: errno=%d", CAMERA_DEVICE_PATH, errno);
        err = ESP_FAIL;
        goto fail;
    }

    err = configure_video_device();
    if (err != ESP_OK) {
        goto fail;
    }

    err = allocate_buffers();
    if (err != ESP_OK) {
        goto fail;
    }

    err = queue_capture_buffers();
    if (err != ESP_OK) {
        goto fail;
    }

    lock_camera();
    s_camera.ready = true;
    s_camera.last_error = ESP_OK;
    unlock_camera();
    ESP_LOGI(TAG, "Camera ready");
    return ESP_OK;

fail:
    if (s_camera.fd >= 0) {
        close(s_camera.fd);
        s_camera.fd = -1;
    }
    free_buffers();
    lock_camera();
    s_camera.ready = false;
    s_camera.last_error = err;
    unlock_camera();
    return err;
}

bool top_spot_camera_is_ready(void)
{
    lock_camera();
    bool ready = s_camera.ready;
    unlock_camera();
    return ready;
}

const char *top_spot_camera_status_text(void)
{
    return top_spot_camera_is_ready() ? "Camera Ready" : "Camera Problem";
}

esp_err_t top_spot_camera_start_preview(lv_obj_t *canvas)
{
    esp_err_t err = top_spot_camera_init();
    if (err != ESP_OK) {
        return err;
    }

    lock_camera();
    if (s_camera.streaming) {
        s_camera.preview_canvas = canvas;
        unlock_camera();
        return ESP_OK;
    }
    if (s_camera.preview_task != NULL) {
        unlock_camera();
        ESP_LOGW(TAG, "Preview start rejected; previous preview worker is still stopping");
        return ESP_ERR_INVALID_STATE;
    }
    s_camera.preview_canvas = canvas;
    s_camera.preview_session_id++;
    if (s_camera.preview_session_id == 0) {
        s_camera.preview_session_id = 1;
    }
    s_camera.fresh_frame_ready = false;
    s_camera.buffer_log_count = 0;
    uint32_t session_id = s_camera.preview_session_id;
    bool buffers_queued = s_camera.buffers_queued;
    unlock_camera();

    ESP_LOGI(TAG, "Camera preview session %lu starting", (unsigned long)session_id);
    if (!buffers_queued) {
        err = requeue_capture_buffers();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Preview buffer requeue failed for session %lu: %s",
                     (unsigned long)session_id, esp_err_to_name(err));
            return err;
        }
    }

    err = set_streaming(true);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(preview_task, "camera_preview",
                                                 CAMERA_PREVIEW_TASK_STACK,
                                                 (void *)(uintptr_t)session_id,
                                                 CAMERA_PREVIEW_TASK_PRIORITY,
                                                 &s_camera.preview_task, 0);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera preview task");
        set_streaming(false);
        lock_camera();
        s_camera.buffers_queued = false;
        unlock_camera();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t top_spot_camera_stop_preview(void)
{
    lock_camera();
    bool was_streaming = s_camera.streaming;
    s_camera.streaming = false;
    s_camera.preview_canvas = NULL;
    TaskHandle_t task = s_camera.preview_task;
    unlock_camera();

    if (task != NULL) {
        ESP_LOGI(TAG, "Preview worker stopping");
        bool done = false;
        for (int i = 0; i < 50; i++) {
            lock_camera();
            done = s_camera.preview_task == NULL;
            unlock_camera();
            if (done) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!done) {
            ESP_LOGW(TAG, "Preview worker did not stop before stream shutdown");
        }
    }

    if (was_streaming && s_camera.fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(s_camera.fd, VIDIOC_STREAMOFF, &type) != 0) {
            ESP_LOGW(TAG, "VIDIOC_STREAMOFF failed while stopping preview: errno=%d", errno);
        } else {
            ESP_LOGI(TAG, "Camera stream stopped");
        }
        lock_camera();
        s_camera.buffers_queued = false;
        unlock_camera();
    }
    ESP_LOGI(TAG, "Camera preview session ended");
    return ESP_OK;
}

esp_err_t top_spot_camera_capture_jpeg(top_spot_photo_type_t type,
                                       const char *entry_number,
                                       char *out_path,
                                       size_t out_path_size,
                                       size_t *out_file_size)
{
    if (!top_spot_camera_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!top_spot_storage_is_ready()) {
        ESP_LOGE(TAG, "Storage is not ready; skipping capture");
        return ESP_ERR_INVALID_STATE;
    }

    lock_camera();
    bool can_capture = s_camera.streaming &&
                       s_camera.fresh_frame_ready &&
                       s_camera.fresh_frame_session_id == s_camera.preview_session_id;
    uint32_t session_id = s_camera.preview_session_id;
    unlock_camera();
    if (!can_capture) {
        ESP_LOGW(TAG, "Capture rejected; no fresh frame for preview session %lu",
                 (unsigned long)session_id);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Camera capture requested from fresh frame for session %lu",
             (unsigned long)session_id);

    uint8_t *raw_copy = heap_caps_aligned_alloc(
        s_camera.cache_align, s_camera.frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw_copy == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lock_camera();
    memcpy(raw_copy, s_camera.capture_frame, s_camera.frame_size);
    uint32_t width = s_camera.width;
    uint32_t height = s_camera.height;
    size_t frame_size = s_camera.frame_size;
    unlock_camera();

    char path[128];
    esp_err_t err = make_unique_path(type, entry_number, path, sizeof(path));
    if (err != ESP_OK) {
        heap_caps_free(raw_copy);
        return err;
    }

    jpeg_encoder_handle_t jpeg = NULL;
    jpeg_encode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = CAMERA_JPEG_TIMEOUT_MS,
    };
    err = jpeg_new_encoder_engine(&engine_cfg, &jpeg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "jpeg_new_encoder_engine failed: %s", esp_err_to_name(err));
        heap_caps_free(raw_copy);
        return err;
    }

    jpeg_encode_cfg_t encode_cfg = {
        .height = height,
        .width = width,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = CAMERA_JPEG_QUALITY,
        .pixel_reverse = false,
    };
    jpeg_encode_memory_alloc_cfg_t out_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    size_t out_capacity = 0;
    uint8_t *jpeg_buf = jpeg_alloc_encoder_mem(frame_size, &out_mem_cfg, &out_capacity);
    if (jpeg_buf == NULL) {
        jpeg_del_encoder_engine(jpeg);
        heap_caps_free(raw_copy);
        return ESP_ERR_NO_MEM;
    }

    uint32_t jpeg_size = 0;
    err = jpeg_encoder_process(jpeg, &encode_cfg, raw_copy, frame_size,
                               jpeg_buf, out_capacity, &jpeg_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "jpeg_encoder_process failed: %s", esp_err_to_name(err));
        free(jpeg_buf);
        jpeg_del_encoder_engine(jpeg);
        heap_caps_free(raw_copy);
        return err;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Open JPEG for write failed: %s errno=%d", path, errno);
        err = ESP_FAIL;
    } else {
        size_t written = fwrite(jpeg_buf, 1, jpeg_size, file);
        if (written != jpeg_size) {
            ESP_LOGE(TAG, "JPEG write failed: wrote %u of %u bytes", (unsigned)written, (unsigned)jpeg_size);
            err = ESP_FAIL;
        }
        if (fclose(file) != 0) {
            ESP_LOGE(TAG, "JPEG close failed: errno=%d", errno);
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK) {
        err = verify_file(path, out_file_size);
    }
    if (err == ESP_OK && out_path != NULL && out_path_size > 0) {
        strncpy(out_path, path, out_path_size - 1);
        out_path[out_path_size - 1] = '\0';
    }

    free(jpeg_buf);
    jpeg_del_encoder_engine(jpeg);
    heap_caps_free(raw_copy);
    return err;
}

void top_spot_camera_deinit(void)
{
    top_spot_camera_stop_preview();
    if (s_camera.fd >= 0) {
        close(s_camera.fd);
        s_camera.fd = -1;
    }
    free_buffers();
    lock_camera();
    s_camera.ready = false;
    s_camera.init_attempted = false;
    s_camera.last_error = ESP_ERR_INVALID_STATE;
    unlock_camera();
}
