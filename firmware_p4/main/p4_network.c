#include "p4_network.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "p4_network_config.h"
#include "car_status_store.h"
#include "record_store.h"
#include "show_store.h"

static const char *TAG = "top_spot_network";

#define NETWORK_TASK_STACK (10 * 1024)
#define NETWORK_TASK_PRIORITY 3
#define NETWORK_CHECK_CONNECTED_MS 15000
#define NETWORK_CHECK_DISCONNECTED_MS 5000
#define HTTP_TIMEOUT_MS 2500
#define HTTP_MAX_RESPONSE_BYTES 65536
#define NETWORK_STACK_LOG_LIMIT 8
#define MAX_RECORD_UPLOADS_PER_SYNC 4
#define NETWORK_INTERNAL_FALLBACK_LIMIT 4096

typedef struct {
    bool initialized;
    bool wifi_started;
    bool wifi_connected;
    bool homebase_connected;
    int rssi_dbm;
    top_spot_wifi_level_t wifi_level;
    char ip_addr[16];
    char homebase_url[96];
    char handheld_id[16];
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
} network_state_t;

static network_state_t s_net = {
    .rssi_dbm = -127,
    .wifi_level = TOP_SPOT_WIFI_DISCONNECTED,
};
static top_spot_app_state_t *s_app_state;
static int s_stack_log_count;

static esp_err_t ensure_mutex(void)
{
    if (s_net.mutex == NULL) {
        s_net.mutex = xSemaphoreCreateMutex();
        if (s_net.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void lock_network(void)
{
    if (s_net.mutex != NULL) {
        xSemaphoreTake(s_net.mutex, portMAX_DELAY);
    }
}

static void unlock_network(void)
{
    if (s_net.mutex != NULL) {
        xSemaphoreGive(s_net.mutex);
    }
}

static top_spot_wifi_level_t level_from_rssi(int rssi)
{
    if (rssi >= -55) {
        return TOP_SPOT_WIFI_STRONG;
    }
    if (rssi >= -67) {
        return TOP_SPOT_WIFI_GOOD;
    }
    if (rssi >= -75) {
        return TOP_SPOT_WIFI_MODERATE;
    }
    return TOP_SPOT_WIFI_WEAK;
}

static void set_homebase_connected(bool connected)
{
    lock_network();
    if (s_net.homebase_connected != connected) {
        ESP_LOGI(TAG, "Home Base %s", connected ? "connected" : "unavailable");
    }
    s_net.homebase_connected = connected;
    unlock_network();
}

static void make_handheld_id(void)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Default eFuse MAC read failed: %s; falling back to Wi-Fi STA MAC",
                 esp_err_to_name(err));
        err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read hardware MAC for handheld identity: %s", esp_err_to_name(err));
        snprintf(s_net.handheld_id, sizeof(s_net.handheld_id), "TS-HH-UNKNOWN");
        return;
    }
    snprintf(s_net.handheld_id, sizeof(s_net.handheld_id), "TS-HH-%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Handheld ID: %s", s_net.handheld_id);
}

static void update_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        top_spot_wifi_level_t level = level_from_rssi(ap.rssi);
        lock_network();
        bool changed = s_net.rssi_dbm != ap.rssi || s_net.wifi_level != level;
        s_net.rssi_dbm = ap.rssi;
        s_net.wifi_level = level;
        unlock_network();
        if (changed) {
            ESP_LOGI(TAG, "Wi-Fi RSSI: %d dBm level=%d", ap.rssi, level);
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station starting");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        lock_network();
        s_net.wifi_connected = false;
        s_net.homebase_connected = false;
        s_net.rssi_dbm = -127;
        s_net.wifi_level = TOP_SPOT_WIFI_DISCONNECTED;
        s_net.ip_addr[0] = '\0';
        unlock_network();
        ESP_LOGW(TAG, "Wi-Fi lost; reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        lock_network();
        s_net.wifi_connected = true;
        snprintf(s_net.ip_addr, sizeof(s_net.ip_addr), "%s", ip);
        unlock_network();
        ESP_LOGI(TAG, "Wi-Fi connected, IP assigned: %s", ip);
        update_rssi();
    }
}

static esp_err_t start_wifi(void)
{
    if (TOP_SPOT_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty in p4_network_config.h; networking disabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            wifi_event_handler, NULL, NULL),
                        TAG, "register Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            wifi_event_handler, NULL, NULL),
                        TAG, "register IP handler failed");

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", TOP_SPOT_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", TOP_SPOT_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    lock_network();
    s_net.wifi_started = true;
    unlock_network();
    ESP_LOGI(TAG, "Wi-Fi configured for SSID '%s'", TOP_SPOT_WIFI_SSID);
    return ESP_OK;
}

static bool wifi_is_connected(void)
{
    lock_network();
    bool connected = s_net.wifi_connected;
    unlock_network();
    return connected;
}

static bool set_homebase_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return false;
    }
    lock_network();
    bool changed = strncmp(s_net.homebase_url, url, sizeof(s_net.homebase_url)) != 0;
    snprintf(s_net.homebase_url, sizeof(s_net.homebase_url), "%s", url);
    unlock_network();
    if (changed) {
        ESP_LOGI(TAG, "Home Base discovered at %s", url);
    }
    return true;
}

static bool discover_homebase_hostname(void)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result = NULL;
    int rc = getaddrinfo(TOP_SPOT_HOMEBASE_HOSTNAME, NULL, &hints, &result);
    if (rc != 0 || result == NULL) {
        ESP_LOGI(TAG, "Home Base hostname discovery failed for %s: %d", TOP_SPOT_HOMEBASE_HOSTNAME, rc);
        return false;
    }
    freeaddrinfo(result);

    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d", TOP_SPOT_HOMEBASE_HOSTNAME, TOP_SPOT_HOMEBASE_PORT);
    return set_homebase_url(url);
}

static bool discover_homebase_udp(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
        ESP_LOGW(TAG, "UDP discovery socket failed: errno=%d", errno);
        return false;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct timeval timeout = {
        .tv_sec = 1,
        .tv_usec = 500000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(TOP_SPOT_HOMEBASE_DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_BROADCAST),
    };
    const char *probe = "{\"service\":\"topspot-handheld\",\"query\":\"discover\",\"protocol_version\":1}";
    int sent = sendto(fd, probe, strlen(probe), 0, (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0) {
        ESP_LOGW(TAG, "UDP discovery send failed: errno=%d", errno);
        close(fd);
        return false;
    }

    char buf[192] = {0};
    int len = recv(fd, buf, sizeof(buf) - 1, 0);
    close(fd);
    if (len <= 0) {
        ESP_LOGI(TAG, "Home Base UDP discovery timed out");
        return false;
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "Home Base UDP discovery returned invalid JSON");
        return false;
    }

    cJSON *service = cJSON_GetObjectItem(root, "service");
    cJSON *base_url = cJSON_GetObjectItem(root, "base_url");
    bool ok = cJSON_IsString(service) &&
              strcmp(service->valuestring, "topspot-homebase") == 0 &&
              cJSON_IsString(base_url) &&
              set_homebase_url(base_url->valuestring);
    cJSON_Delete(root);
    return ok;
}

static bool discover_homebase(void)
{
    if (discover_homebase_hostname()) {
        return true;
    }
    return discover_homebase_udp();
}

static bool http_get_health(const char *base_url)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/v1/health", base_url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200) {
        ESP_LOGI(TAG, "Home Base health failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }
    return true;
}

static bool post_handshake(const char *base_url)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/v1/handshake", base_url);

    char id[16];
    int rssi;
    lock_network();
    snprintf(id, sizeof(id), "%s", s_net.handheld_id);
    rssi = s_net.rssi_dbm;
    unlock_network();

    char body[192];
    snprintf(body, sizeof(body),
             "{\"handheld_id\":\"%s\",\"firmware_version\":\"p4-local-0.1\","
             "\"protocol_version\":1,\"rssi_dbm\":%d}",
             id, rssi);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Home Base handshake failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }

    ESP_LOGI(TAG, "Home Base handshake succeeded for %s rssi=%d", id, rssi);
    return true;
}

void top_spot_network_attach_state(top_spot_app_state_t *state)
{
    lock_network();
    s_app_state = state;
    unlock_network();
}

static top_spot_app_state_t *get_app_state(void)
{
    top_spot_app_state_t *state;
    lock_network();
    state = s_app_state;
    unlock_network();
    return state;
}

static int local_record_sequence(const char *local_record_id)
{
    if (local_record_id == NULL || strncmp(local_record_id, "local-", 6) != 0) {
        return (int)(esp_timer_get_time() / 1000);
    }
    char *end = NULL;
    long value = strtol(local_record_id + 6, &end, 10);
    if (end == local_record_id + 6 || *end != '\0' || value <= 0) {
        return (int)(esp_timer_get_time() / 1000);
    }
    return (int)value;
}

static cJSON *record_to_submission_json(const top_spot_record_t *record)
{
    cJSON *item = cJSON_CreateObject();
    if (item == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(item, "local_record_id", record->local_record_id);
    cJSON_AddStringToObject(item, "entry_number", record->entry_number);
    cJSON_AddNumberToObject(item, "closed_at_uptime_ms",
                            record->closed_at_uptime_ms > 0
                                ? record->closed_at_uptime_ms
                                : local_record_sequence(record->local_record_id));
    cJSON_AddStringToObject(item, "participant", record->participant);
    cJSON_AddStringToObject(item, "year", record->year);
    cJSON_AddStringToObject(item, "make", record->make);
    cJSON_AddStringToObject(item, "model", record->model);
    cJSON_AddStringToObject(item, "vehicle_type", "Car");
    cJSON_AddBoolToObject(item, "make_manually_entered", false);
    cJSON_AddBoolToObject(item, "model_manually_entered", false);
    cJSON_AddNumberToObject(item, "score_range_max",
                            record->score_range_max > 0 ? record->score_range_max : TOP_SPOT_DEFAULT_SCORE_MAX);

    cJSON *scores = cJSON_AddArrayToObject(item, "scores");
    for (int i = 0; i < record->score_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        cJSON *score = cJSON_CreateObject();
        cJSON_AddNumberToObject(score, "category_id", record->score_category_ids[i]);
        cJSON_AddNumberToObject(score, "points", record->scores[i]);
        cJSON_AddItemToArray(scores, score);
    }

    cJSON *nominations = cJSON_AddArrayToObject(item, "nominations");
    for (int i = 0; i < record->award_count && i < TOP_SPOT_MAX_AWARDS; i++) {
        if (record->nominations[i]) {
            cJSON_AddItemToArray(nominations, cJSON_CreateNumber(record->award_ids[i]));
        }
    }
    cJSON_AddStringToObject(item, "vehicle_photo_path", record->vehicle_photo_path);
    cJSON_AddStringToObject(item, "judge_sheet_photo_path", record->judge_sheet_photo_path);
    return item;
}

static void *network_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL && size <= NETWORK_INTERNAL_FALLBACK_LIMIT) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_8BIT);
    }
    if (ptr == NULL) {
        ESP_LOGW(TAG, "NETWORK MEM: allocation failed size=%u psram_largest=%u internal_largest=%u dma_largest=%u",
                 (unsigned)size,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
    return ptr;
}

static void log_memory_headroom(const char *label)
{
    if (s_stack_log_count >= NETWORK_STACK_LOG_LIMIT) {
        return;
    }
    ESP_LOGI(TAG,
             "NETWORK MEM: %s internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u psram_largest=%u",
             label,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

static void log_network_stack_headroom(const char *label)
{
    if (s_stack_log_count >= NETWORK_STACK_LOG_LIMIT) {
        return;
    }
    UBaseType_t words = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Network task stack high-water %s: %u words (%u bytes)",
             label, (unsigned)words, (unsigned)(words * sizeof(StackType_t)));
}

static char *http_post_json_read(const char *url, const char *body, int *out_status)
{
    size_t body_len = strlen(body);
    if (body_len > INT_MAX) {
        ESP_LOGW(TAG, "POST %s body too large: %u", url, (unsigned)body_len);
        return NULL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return NULL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(client, (int)body_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST %s open failed: err=%s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }

    int written = esp_http_client_write(client, body, (int)body_len);
    if (written != (int)body_len) {
        ESP_LOGW(TAG, "POST %s body write failed: written=%d expected=%u errno=%d",
                 url, written, (unsigned)body_len, errno);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int64_t header_content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    if (out_status != NULL) {
        *out_status = status;
    }
    if (header_content_length < 0 || status != 200) {
        ESP_LOGW(TAG, "POST %s failed: header_len=%lld status=%d errno=%d",
                 url, (long long)header_content_length, status, errno);
        esp_http_client_cleanup(client);
        return NULL;
    }

    log_memory_headroom("before sync response allocation");
    char *response = network_alloc(HTTP_MAX_RESPONSE_BYTES);
    if (response == NULL) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    ESP_LOGI(TAG, "HOME BASE: sync HTTP status=%d content_length=%lld chunked=%d",
             status, (long long)content_length, esp_http_client_is_chunked_response(client));

    size_t total = 0;
    bool overflow = false;
    bool incomplete = false;
    int chunk_log_count = 0;
    while (!esp_http_client_is_complete_data_received(client)) {
        if (total >= HTTP_MAX_RESPONSE_BYTES - 1) {
            overflow = true;
            break;
        }

        int read_len = esp_http_client_read(client, response + total,
                                            (int)(HTTP_MAX_RESPONSE_BYTES - 1 - total));
        if (read_len < 0) {
            ESP_LOGW(TAG, "HOME BASE: sync body read failed after %u bytes: ret=%d errno=%d",
                     (unsigned)total, read_len, errno);
            free(response);
            esp_http_client_cleanup(client);
            return NULL;
        }
        if (read_len == 0) {
            incomplete = true;
            break;
        }

        total += (size_t)read_len;
        if (chunk_log_count < 3) {
            ESP_LOGI(TAG, "HOME BASE: sync body received chunk=%d total=%u",
                     read_len, (unsigned)total);
            chunk_log_count++;
        }
    }

    if (!esp_http_client_is_complete_data_received(client) && !overflow) {
        incomplete = true;
    }

    esp_http_client_cleanup(client);
    if (overflow) {
        ESP_LOGW(TAG, "HOME BASE: sync response overflow received=%u capacity=%d content_length=%lld",
                 (unsigned)total, HTTP_MAX_RESPONSE_BYTES, (long long)content_length);
        free(response);
        return NULL;
    }
    if (incomplete) {
        ESP_LOGW(TAG, "HOME BASE: sync response incomplete received=%u content_length=%lld",
                 (unsigned)total, (long long)content_length);
        free(response);
        return NULL;
    }

    response[total] = '\0';
    ESP_LOGI(TAG, "HOME BASE: sync response complete bytes=%u content_length=%lld",
             (unsigned)total, (long long)content_length);
    log_memory_headroom("after HTTP body receive complete");
    return response;
}

static cJSON *parse_sync_response_json(const char *response, const char *phase)
{
    log_memory_headroom("before cJSON parse");
    cJSON *json = cJSON_Parse(response);
    if (json == NULL) {
        const char *error = cJSON_GetErrorPtr();
        size_t len = strlen(response);
        size_t offset = error != NULL && error >= response ? (size_t)(error - response) : 0;
        ESP_LOGW(TAG, "SYNC PARSE: %s malformed JSON len=%u error_offset=%u",
                 phase, (unsigned)len, (unsigned)offset);
        char excerpt[241];
        size_t copy_len = len < sizeof(excerpt) - 1 ? len : sizeof(excerpt) - 1;
        memcpy(excerpt, response, copy_len);
        excerpt[copy_len] = '\0';
        ESP_LOGW(TAG, "SYNC PARSE: response excerpt: %s", excerpt);
        return NULL;
    }
    log_memory_headroom("after cJSON parse");
    return json;
}

static const char *json_type_name(const cJSON *item)
{
    if (item == NULL) {
        return "missing";
    }
    if (cJSON_IsNull(item)) {
        return "null";
    }
    if (cJSON_IsBool(item)) {
        return "bool";
    }
    if (cJSON_IsNumber(item)) {
        return "number";
    }
    if (cJSON_IsString(item)) {
        return "string";
    }
    if (cJSON_IsArray(item)) {
        return "array";
    }
    if (cJSON_IsObject(item)) {
        return "object";
    }
    return "unknown";
}

static void log_configuration_diagnostics(cJSON *configuration, const char *phase, const char *reason)
{
    ESP_LOGW(TAG, "SYNC PARSE: %s configuration diagnostics: %s type=%s",
             phase, reason, json_type_name(configuration));
    if (!cJSON_IsObject(configuration)) {
        return;
    }

    char keys[192] = {0};
    size_t used = 0;
    int count = 0;
    cJSON *child = NULL;
    cJSON_ArrayForEach(child, configuration) {
        const char *key = child->string != NULL ? child->string : "<null>";
        int written = snprintf(keys + used, sizeof(keys) - used, "%s%s:%s",
                               used > 0 ? "," : "", key, json_type_name(child));
        if (written < 0 || (size_t)written >= sizeof(keys) - used) {
            used = sizeof(keys) - 1;
            break;
        }
        used += (size_t)written;
        count++;
        if (count >= 12) {
            snprintf(keys + used, sizeof(keys) - used, ",...");
            break;
        }
    }
    ESP_LOGW(TAG, "SYNC PARSE: %s configuration keys=%s", phase, keys);

    cJSON *show_id = cJSON_GetObjectItemCaseSensitive(configuration, "show_id");
    if (cJSON_IsNumber(show_id)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s configuration.show_id type=number value=%d",
                 phase, show_id->valueint);
    } else if (cJSON_IsString(show_id)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s configuration.show_id type=string value=%s",
                 phase, show_id->valuestring);
    } else {
        ESP_LOGW(TAG, "SYNC PARSE: %s configuration.show_id type=%s",
                 phase, json_type_name(show_id));
    }

    char *printed = cJSON_PrintUnformatted(configuration);
    if (printed != NULL) {
        char excerpt[241];
        size_t len = strlen(printed);
        size_t copy_len = len < sizeof(excerpt) - 1 ? len : sizeof(excerpt) - 1;
        memcpy(excerpt, printed, copy_len);
        excerpt[copy_len] = '\0';
        ESP_LOGW(TAG, "SYNC PARSE: %s configuration excerpt: %s", phase, excerpt);
        cJSON_free(printed);
    }
}

static bool validate_sync_response_json(cJSON *json, const char *phase)
{
    if (!cJSON_IsObject(json)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s top-level response is not an object", phase);
        return false;
    }

    cJSON *server_time = cJSON_GetObjectItemCaseSensitive(json, "server_time");
    cJSON *config_revision = cJSON_GetObjectItemCaseSensitive(json, "config_revision");
    cJSON *data_revision = cJSON_GetObjectItemCaseSensitive(json, "data_revision");
    cJSON *configuration = cJSON_GetObjectItemCaseSensitive(json, "configuration");
    cJSON *cars = cJSON_GetObjectItemCaseSensitive(json, "cars");
    cJSON *results = cJSON_GetObjectItemCaseSensitive(json, "results");
    cJSON *summary = cJSON_GetObjectItemCaseSensitive(json, "summary");

    if (!cJSON_IsString(server_time)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s missing/string mismatch: server_time", phase);
        return false;
    }
    if (!cJSON_IsNumber(config_revision)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s missing/number mismatch: config_revision", phase);
        return false;
    }
    if (!cJSON_IsNumber(data_revision)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s missing/number mismatch: data_revision", phase);
        return false;
    }
    if (!cJSON_IsNull(configuration) && !cJSON_IsObject(configuration)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s configuration is not object/null", phase);
        return false;
    }
    if (!cJSON_IsArray(cars)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s cars missing/not array", phase);
        return false;
    }
    if (!cJSON_IsArray(results)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s results missing/not array", phase);
        return false;
    }
    if (!cJSON_IsObject(summary)) {
        ESP_LOGW(TAG, "SYNC PARSE: %s summary missing/not object", phase);
        return false;
    }

    if (cJSON_IsObject(configuration)) {
        cJSON *show_id = cJSON_GetObjectItemCaseSensitive(configuration, "show_id");
        cJSON *show_name = cJSON_GetObjectItemCaseSensitive(configuration, "show_name");
        cJSON *show_config_revision = cJSON_GetObjectItemCaseSensitive(configuration, "config_revision");
        cJSON *score_max = cJSON_GetObjectItemCaseSensitive(configuration, "score_range_max");
        cJSON *categories = cJSON_GetObjectItemCaseSensitive(configuration, "categories");
        cJSON *awards = cJSON_GetObjectItemCaseSensitive(configuration, "judge_chosen_awards");

        if (!cJSON_IsNumber(show_id)) {
            log_configuration_diagnostics(configuration, phase, "show_id missing/not number");
            ESP_LOGW(TAG, "SYNC PARSE: %s configuration.show_id missing/not number", phase);
            return false;
        }
        if (!cJSON_IsString(show_name)) {
            ESP_LOGW(TAG, "SYNC PARSE: %s configuration.show_name missing/not string", phase);
            return false;
        }
        if (!cJSON_IsNumber(show_config_revision)) {
            ESP_LOGW(TAG, "SYNC PARSE: %s configuration.config_revision missing/not number", phase);
            return false;
        }
        if (!cJSON_IsNumber(score_max)) {
            ESP_LOGW(TAG, "SYNC PARSE: %s configuration.score_range_max missing/not number", phase);
            return false;
        }
        if (!cJSON_IsArray(categories)) {
            ESP_LOGW(TAG, "SYNC PARSE: %s configuration.categories missing/not array", phase);
            return false;
        }
        if (!cJSON_IsArray(awards)) {
            ESP_LOGW(TAG, "SYNC PARSE: %s configuration.judge_chosen_awards missing/not array", phase);
            return false;
        }

        int category_count = cJSON_GetArraySize(categories);
        for (int i = 0; i < category_count; i++) {
            cJSON *category = cJSON_GetArrayItem(categories, i);
            if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(category, "id"))) {
                ESP_LOGW(TAG, "SYNC PARSE: %s category[%d] missing id", phase, i);
                return false;
            }
            if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(category, "name"))) {
                ESP_LOGW(TAG, "SYNC PARSE: %s category[%d] missing name", phase, i);
                return false;
            }
            if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(category, "sort_order"))) {
                ESP_LOGW(TAG, "SYNC PARSE: %s category[%d] missing sort_order", phase, i);
                return false;
            }
        }

        int award_count = cJSON_GetArraySize(awards);
        for (int i = 0; i < award_count; i++) {
            cJSON *award = cJSON_GetArrayItem(awards, i);
            if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(award, "id"))) {
                ESP_LOGW(TAG, "SYNC PARSE: %s award[%d] missing id", phase, i);
                return false;
            }
            if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(award, "name"))) {
                ESP_LOGW(TAG, "SYNC PARSE: %s award[%d] missing name", phase, i);
                return false;
            }
        }
    }

    return true;
}

static bool parse_configuration(cJSON *configuration, top_spot_show_config_t *show,
                                int config_revision, int data_revision)
{
    if (!cJSON_IsObject(configuration)) {
        return false;
    }
    memset(show, 0, sizeof(*show));
    show->loaded = true;
    cJSON *show_id = cJSON_GetObjectItemCaseSensitive(configuration, "show_id");
    cJSON *show_name = cJSON_GetObjectItemCaseSensitive(configuration, "show_name");
    cJSON *show_date = cJSON_GetObjectItemCaseSensitive(configuration, "show_date");
    cJSON *score_max = cJSON_GetObjectItemCaseSensitive(configuration, "score_range_max");
    cJSON *cfg_rev = cJSON_GetObjectItemCaseSensitive(configuration, "config_revision");
    show->show_id = cJSON_IsNumber(show_id) ? show_id->valueint : 0;
    snprintf(show->show_name, sizeof(show->show_name), "%s",
             cJSON_IsString(show_name) ? show_name->valuestring : "");
    snprintf(show->show_date, sizeof(show->show_date), "%s",
             cJSON_IsString(show_date) ? show_date->valuestring : "");
    show->config_revision = cJSON_IsNumber(cfg_rev) ? cfg_rev->valueint : config_revision;
    show->data_revision = data_revision;
    show->score_min = 1;
    show->score_max = cJSON_IsNumber(score_max) ? score_max->valueint : TOP_SPOT_DEFAULT_SCORE_MAX;
    if (show->score_max <= 0 || show->score_max > TOP_SPOT_MAX_SCORE_RANGE) {
        ESP_LOGW(TAG, "SHOW CONFIG: invalid score_range_max=%d; using fallback %d",
                 show->score_max, TOP_SPOT_DEFAULT_SCORE_MAX);
        show->score_max = TOP_SPOT_DEFAULT_SCORE_MAX;
    }

    cJSON *categories = cJSON_GetObjectItemCaseSensitive(configuration, "categories");
    if (cJSON_IsArray(categories)) {
        cJSON *category = NULL;
        cJSON_ArrayForEach(category, categories) {
            if (show->category_count >= TOP_SPOT_MAX_CATEGORIES) {
                break;
            }
            cJSON *id = cJSON_GetObjectItemCaseSensitive(category, "id");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(category, "name");
            cJSON *sort_order = cJSON_GetObjectItemCaseSensitive(category, "sort_order");
            top_spot_category_t *dest = &show->categories[show->category_count++];
            dest->id = cJSON_IsNumber(id) ? id->valueint : 0;
            snprintf(dest->key, sizeof(dest->key), "cat_%d", dest->id);
            snprintf(dest->name, sizeof(dest->name), "%s", cJSON_IsString(name) ? name->valuestring : "Category");
            dest->sort_order = cJSON_IsNumber(sort_order) ? sort_order->valueint : show->category_count - 1;
        }
    }

    cJSON *awards = cJSON_GetObjectItemCaseSensitive(configuration, "judge_chosen_awards");
    if (cJSON_IsArray(awards)) {
        cJSON *award = NULL;
        cJSON_ArrayForEach(award, awards) {
            if (show->award_count >= TOP_SPOT_MAX_AWARDS) {
                break;
            }
            cJSON *id = cJSON_GetObjectItemCaseSensitive(award, "id");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(award, "name");
            top_spot_award_t *dest = &show->awards[show->award_count++];
            dest->id = cJSON_IsNumber(id) ? id->valueint : 0;
            snprintf(dest->key, sizeof(dest->key), "award_%d", dest->id);
            snprintf(dest->name, sizeof(dest->name), "%s", cJSON_IsString(name) ? name->valuestring : "Award");
        }
    }
    return show->show_name[0] != '\0' && show->category_count > 0;
}

static bool apply_show_from_sync_response(top_spot_app_state_t *state, cJSON *json,
                                          top_spot_show_config_t *cache_snapshot,
                                          bool *cache_needed)
{
    if (cache_needed != NULL) {
        *cache_needed = false;
    }
    cJSON *config_revision = cJSON_GetObjectItemCaseSensitive(json, "config_revision");
    cJSON *data_revision = cJSON_GetObjectItemCaseSensitive(json, "data_revision");
    int new_config_revision = cJSON_IsNumber(config_revision) ? config_revision->valueint : state->show.config_revision;
    int new_data_revision = cJSON_IsNumber(data_revision) ? data_revision->valueint : state->show.data_revision;

    cJSON *configuration = cJSON_GetObjectItemCaseSensitive(json, "configuration");
    if (cJSON_IsObject(configuration)) {
        log_memory_headroom("before show config extraction");
        top_spot_show_config_t *downloaded = network_alloc(sizeof(*downloaded));
        if (downloaded == NULL) {
            ESP_LOGE(TAG, "SHOW CONFIG: unable to allocate downloaded show config");
            return false;
        }
        bool parsed = parse_configuration(configuration, downloaded, new_config_revision, new_data_revision);
        if (parsed) {
            ESP_LOGI(TAG, "SHOW CONFIG: Active show config received: %s revision=%d",
                     downloaded->show_name, downloaded->config_revision);
            state->show = *downloaded;
            if (state->current.entry_number[0] == '\0') {
                top_spot_apply_show_to_current(&state->current, &state->show);
            }
            if (cache_snapshot != NULL) {
                *cache_snapshot = state->show;
                if (cache_needed != NULL) {
                    *cache_needed = true;
                }
            }
        } else {
            ESP_LOGW(TAG, "SHOW CONFIG: Received invalid active show configuration");
        }
        free(downloaded);
        return parsed;
    }

    state->show.config_revision = new_config_revision;
    state->show.data_revision = new_data_revision;
    if (state->show.category_count == 0) {
        ESP_LOGI(TAG, "SHOW CONFIG: No active show configuration in sync response");
    }
    return state->show.category_count > 0;
}

static void apply_car_status_from_sync_response(top_spot_app_state_t *state, cJSON *json)
{
    cJSON *data_revision = cJSON_GetObjectItemCaseSensitive(json, "data_revision");
    cJSON *cars = cJSON_GetObjectItemCaseSensitive(json, "cars");
    cJSON *sync_mode = cJSON_GetObjectItemCaseSensitive(json, "sync_mode");
    int new_data_revision = cJSON_IsNumber(data_revision) ? data_revision->valueint : state->show.data_revision;
    int car_delta_count = cJSON_IsArray(cars) ? cJSON_GetArraySize(cars) : 0;
    if (car_delta_count <= 0 || state->show.show_id <= 0) {
        return;
    }
    bool full_snapshot = cJSON_IsString(sync_mode) && strcmp(sync_mode->valuestring, "FULL") == 0;
    log_network_stack_headroom("before car-status apply_sync");
    esp_err_t err = top_spot_car_status_store_apply_sync(state, cars, state->show.show_id, new_data_revision, full_snapshot);
    log_network_stack_headroom("after car-status apply_sync");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SHOW STATUS: applied %d car updates mode=%s revision=%d",
                 car_delta_count, full_snapshot ? "FULL" : "DELTA", new_data_revision);
    } else {
        ESP_LOGW(TAG, "SHOW STATUS: car status update failed: %s", esp_err_to_name(err));
    }
}

static void persist_active_show_cache(const top_spot_show_config_t *show)
{
    log_memory_headroom("before show-cache write");
    esp_err_t cache_err = top_spot_show_store_save_active(show);
    log_memory_headroom("after show-cache write");
    if (cache_err == ESP_OK) {
        ESP_LOGI(TAG, "SHOW CACHE: Active show cache persisted");
    } else {
        ESP_LOGW(TAG, "SHOW CACHE: Active show remains active in RAM, but cache update failed: %s",
                 esp_err_to_name(cache_err));
    }
    ESP_LOGI(TAG, "SHOW CONFIG: Active show activated: %s categories=%d awards=%d",
             show->show_name, show->category_count, show->award_count);
}

static bool pending_record_is_syncable(const top_spot_record_t *record,
                                       const top_spot_show_config_t *active_show,
                                       char *reason,
                                       size_t reason_size)
{
    if (record->show_id <= 0) {
        snprintf(reason, reason_size, "missing show_id");
        return false;
    }
    if (record->show_config_revision <= 0) {
        snprintf(reason, reason_size, "missing show_config_revision");
        return false;
    }
    if (record->score_count <= 0) {
        snprintf(reason, reason_size, "missing scores");
        return false;
    }
    int score_range_max = record->score_range_max > 0 ? record->score_range_max : TOP_SPOT_DEFAULT_SCORE_MAX;
    if (score_range_max > TOP_SPOT_MAX_SCORE_RANGE) {
        snprintf(reason, reason_size, "invalid score range %d", score_range_max);
        return false;
    }
    for (int i = 0; i < record->score_count && i < TOP_SPOT_MAX_CATEGORIES; i++) {
        if (record->score_category_ids[i] <= 0) {
            snprintf(reason, reason_size, "missing category id");
            return false;
        }
        if (record->scores[i] < 1 || record->scores[i] > score_range_max) {
            snprintf(reason, reason_size, "score %d outside record range 1-%d",
                     record->scores[i], score_range_max);
            return false;
        }
    }
    if (active_show->category_count <= 0) {
        snprintf(reason, reason_size, "no active show loaded");
        return false;
    }
    if (record->show_id != active_show->show_id) {
        snprintf(reason, reason_size, "record show_id %d does not match active show_id %d",
                 record->show_id, active_show->show_id);
        return false;
    }
    if (record->show_config_revision != active_show->config_revision) {
        snprintf(reason, reason_size, "record show revision %d does not match active revision %d",
                 record->show_config_revision, active_show->config_revision);
        return false;
    }
    return true;
}

static char *post_sync_request(const char *base_url, const char *handheld_id, int rssi,
                               const top_spot_show_config_t *show,
                               const top_spot_record_t *record)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/v1/sync", base_url);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "RECORD UPLOAD: unable to allocate sync request JSON");
        return NULL;
    }
    cJSON_AddStringToObject(root, "handheld_id", handheld_id);
    cJSON_AddNumberToObject(root, "config_revision", show->config_revision);
    cJSON_AddNumberToObject(root, "data_revision", show->data_revision);
    int known_car_count = top_spot_car_status_count();
    cJSON_AddNumberToObject(root, "known_car_count", known_car_count);
    cJSON_AddStringToObject(root, "firmware_version", "p4-local-0.1");
    cJSON_AddNumberToObject(root, "protocol_version", 1);
    cJSON_AddNumberToObject(root, "rssi_dbm", rssi);
    cJSON *submissions = cJSON_AddArrayToObject(root, "submissions");
    if (record != NULL) {
        cJSON *submission = record_to_submission_json(record);
        if (submission == NULL) {
            ESP_LOGE(TAG, "RECORD UPLOAD: unable to serialize record %s", record->local_record_id);
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddItemToArray(submissions, submission);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        ESP_LOGE(TAG, "RECORD UPLOAD: unable to print sync request JSON");
        return NULL;
    }

    ESP_LOGI(TAG, "SYNC REQUEST: show_id=%d data_revision=%d known_car_count=%d",
             show->show_id, show->data_revision, known_car_count);

    int status = 0;
    char *response = http_post_json_read(url, body, &status);
    cJSON_free(body);
    return response;
}

static bool extract_record_upload_result(cJSON *json, char *status, size_t status_size,
                                         char *message, size_t message_size)
{
    if (status == NULL || status_size == 0 || message == NULL || message_size == 0) {
        return false;
    }
    cJSON *results = cJSON_GetObjectItemCaseSensitive(json, "results");
    cJSON *result = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : NULL;
    cJSON *result_status = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "status") : NULL;
    cJSON *result_message = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "message") : NULL;
    if (cJSON_IsString(result_status)) {
        snprintf(status, status_size, "%s", result_status->valuestring);
        snprintf(message, message_size, "%s",
                 cJSON_IsString(result_message) ? result_message->valuestring : result_status->valuestring);
    } else {
        snprintf(status, status_size, "error");
        snprintf(message, message_size, "Missing upload result");
    }
    return true;
}

static bool mark_record_upload_result(const char *pending_id, bool success, const char *message)
{
    esp_err_t mark_err = top_spot_record_store_mark_sync_result(pending_id, success, message);
    if (mark_err != ESP_OK) {
        ESP_LOGE(TAG, "RECORD STORAGE: Record %s upload processed, but local sync_state update failed: %s",
                 pending_id, esp_err_to_name(mark_err));
        return false;
    }
    if (success) {
        ESP_LOGI(TAG, "RECORD UPLOAD: Record %s upload accepted", pending_id);
        ESP_LOGI(TAG, "RECORD STORAGE: Record %s marked synced", pending_id);
    } else {
        ESP_LOGW(TAG, "RECORD UPLOAD: Record %s upload rejected: %s", pending_id, message);
    }
    return true;
}

static bool run_sync_transaction(const char *base_url)
{
    top_spot_app_state_t *state = get_app_state();
    if (state == NULL) {
        return true;
    }
    log_network_stack_headroom("before sync");
    log_memory_headroom("before sync transaction");

    char id[16];
    int rssi;
    lock_network();
    snprintf(id, sizeof(id), "%s", s_net.handheld_id);
    rssi = s_net.rssi_dbm;
    unlock_network();

    char *response = post_sync_request(base_url, id, rssi, &state->show, NULL);
    if (response == NULL) {
        ESP_LOGW(TAG, "HOME BASE: sync request for active show failed");
        return false;
    }
    cJSON *json = parse_sync_response_json(response, "active show");
    free(response);
    log_memory_headroom("after sync response buffer free");
    if (json == NULL) {
        ESP_LOGW(TAG, "HOME BASE: invalid sync response while requesting active show");
        return true;
    }
    if (!validate_sync_response_json(json, "active show")) {
        cJSON_Delete(json);
        return true;
    }
    top_spot_show_config_t active_show_cache = {0};
    bool active_show_cache_needed = false;
    bool active_show_ok = apply_show_from_sync_response(state, json,
                                                        &active_show_cache,
                                                        &active_show_cache_needed);
    if (active_show_ok) {
        apply_car_status_from_sync_response(state, json);
    }
    cJSON_Delete(json);
    log_memory_headroom("after active-show cJSON free");
    if (active_show_cache_needed) {
        persist_active_show_cache(&active_show_cache);
    }
    if (!active_show_ok) {
        return true;
    }

    log_memory_headroom("before record sync processing");
    int pending_count = top_spot_record_store_pending_count();
    ESP_LOGI(TAG, "RECORD UPLOAD: Processing %d pending records", pending_count);

    char attempted_ids[MAX_RECORD_UPLOADS_PER_SYNC][16] = {0};
    size_t attempted_count = 0;
    for (int uploaded = 0; uploaded < MAX_RECORD_UPLOADS_PER_SYNC; uploaded++) {
        top_spot_record_t *pending_record = network_alloc(sizeof(*pending_record));
        if (pending_record == NULL) {
            ESP_LOGE(TAG, "RECORD UPLOAD: unable to allocate pending-record sync snapshot");
            break;
        }
        if (top_spot_record_store_get_pending_excluding(pending_record, attempted_ids, attempted_count) != ESP_OK) {
            free(pending_record);
            break;
        }

        char pending_id[16] = {0};
        snprintf(pending_id, sizeof(pending_id), "%s", pending_record->local_record_id);
        if (attempted_count < MAX_RECORD_UPLOADS_PER_SYNC) {
            snprintf(attempted_ids[attempted_count], sizeof(attempted_ids[attempted_count]), "%s", pending_id);
            attempted_count++;
        }
        char legacy_reason[96] = {0};
        if (!pending_record_is_syncable(pending_record, &state->show, legacy_reason, sizeof(legacy_reason))) {
            ESP_LOGW(TAG, "RECORD UPLOAD: Record %s is legacy/unsyncable: %s",
                     pending_id, legacy_reason);
            esp_err_t legacy_err = top_spot_record_store_mark_legacy(pending_id, legacy_reason);
            if (legacy_err != ESP_OK) {
                ESP_LOGE(TAG, "RECORD STORAGE: Failed to mark legacy record %s: %s",
                         pending_id, esp_err_to_name(legacy_err));
                free(pending_record);
                break;
            }
            free(pending_record);
            continue;
        }

        response = post_sync_request(base_url, id, rssi, &state->show, pending_record);
        free(pending_record);
        if (response == NULL) {
            top_spot_record_store_mark_sync_result(pending_id, false, "Home Base sync failed");
            ESP_LOGW(TAG, "HOME BASE: sync request failed while uploading record %s", pending_id);
            continue;
        }

        json = parse_sync_response_json(response, "record upload");
        free(response);
        log_memory_headroom("after record response buffer free");
        if (json == NULL) {
            top_spot_record_store_mark_sync_result(pending_id, false, "Invalid Home Base sync response");
            ESP_LOGW(TAG, "HOME BASE: invalid sync response while uploading record %s", pending_id);
            continue;
        }
        if (!validate_sync_response_json(json, "record upload")) {
            cJSON_Delete(json);
            top_spot_record_store_mark_sync_result(pending_id, false, "Home Base sync response schema mismatch");
            continue;
        }
        top_spot_show_config_t upload_show_cache = {0};
        bool upload_show_cache_needed = false;
        apply_show_from_sync_response(state, json, &upload_show_cache, &upload_show_cache_needed);
        apply_car_status_from_sync_response(state, json);
        char upload_status[24] = {0};
        char upload_message[96] = {0};
        extract_record_upload_result(json, upload_status, sizeof(upload_status), upload_message, sizeof(upload_message));
        cJSON_Delete(json);
        log_memory_headroom("after record cJSON free");
        if (upload_show_cache_needed) {
            persist_active_show_cache(&upload_show_cache);
        }
        bool upload_success = strcmp(upload_status, "accepted") == 0 ||
                              strcmp(upload_status, "already_recorded") == 0 ||
                              strcmp(upload_status, "flagged_duplicate") == 0;
        if (strcmp(upload_status, "error") == 0) {
            ESP_LOGW(TAG, "RECORD UPLOAD: Record %s permanently rejected: %s",
                     pending_id, upload_message);
            top_spot_record_store_mark_rejected(pending_id, upload_message);
        } else {
            mark_record_upload_result(pending_id, upload_success, upload_message);
        }
    }

    log_network_stack_headroom("after sync");
    s_stack_log_count++;
    return true;
}

static void network_task(void *arg)
{
    (void)arg;
    while (true) {
        if (!wifi_is_connected()) {
            set_homebase_connected(false);
            vTaskDelay(pdMS_TO_TICKS(NETWORK_CHECK_DISCONNECTED_MS));
            continue;
        }

        update_rssi();

        char base_url[96] = {0};
        lock_network();
        snprintf(base_url, sizeof(base_url), "%s", s_net.homebase_url);
        unlock_network();

        if (base_url[0] == '\0' && !discover_homebase()) {
            set_homebase_connected(false);
            vTaskDelay(pdMS_TO_TICKS(NETWORK_CHECK_CONNECTED_MS));
            continue;
        }

        lock_network();
        snprintf(base_url, sizeof(base_url), "%s", s_net.homebase_url);
        unlock_network();

        bool ok = http_get_health(base_url) && post_handshake(base_url) && run_sync_transaction(base_url);
        set_homebase_connected(ok);
        if (!ok) {
            lock_network();
            s_net.homebase_url[0] = '\0';
            unlock_network();
            ESP_LOGI(TAG, "Home Base rediscovery will run on next check");
        }

        vTaskDelay(pdMS_TO_TICKS(NETWORK_CHECK_CONNECTED_MS));
    }
}

esp_err_t top_spot_network_init(void)
{
    ESP_RETURN_ON_ERROR(ensure_mutex(), TAG, "network mutex failed");
    if (s_net.initialized) {
        return ESP_OK;
    }

    make_handheld_id();
    esp_err_t err = start_wifi();
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(network_task, "topspot_network",
                                            NETWORK_TASK_STACK, NULL,
                                            NETWORK_TASK_PRIORITY,
                                            &s_net.task, 0);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_net.initialized = true;
    return ESP_OK;
}

void top_spot_network_get_status(top_spot_network_status_t *out_status)
{
    if (out_status == NULL) {
        return;
    }
    lock_network();
    out_status->wifi_connected = s_net.wifi_connected;
    out_status->homebase_connected = s_net.homebase_connected;
    out_status->rssi_dbm = s_net.rssi_dbm;
    out_status->wifi_level = s_net.wifi_level;
    snprintf(out_status->ip_addr, sizeof(out_status->ip_addr), "%s", s_net.ip_addr);
    snprintf(out_status->homebase_url, sizeof(out_status->homebase_url), "%s", s_net.homebase_url);
    snprintf(out_status->handheld_id, sizeof(out_status->handheld_id), "%s", s_net.handheld_id);
    unlock_network();
}

const char *top_spot_network_wifi_text(void)
{
    static char text[32];
    top_spot_network_status_t status = {0};
    top_spot_network_get_status(&status);
    if (!status.wifi_connected) {
        return "Disconnected";
    }
    snprintf(text, sizeof(text), "Connected (%d dBm)", status.rssi_dbm);
    return text;
}

const char *top_spot_network_homebase_text(void)
{
    top_spot_network_status_t status = {0};
    top_spot_network_get_status(&status);
    if (!status.wifi_connected) {
        return "Unavailable";
    }
    return status.homebase_connected ? "Connected" : "Unavailable";
}

const char *top_spot_network_sync_text(void)
{
    static char text[32];
    int pending = top_spot_record_store_pending_count();
    if (pending == 0) {
        return "All Synced";
    }
    snprintf(text, sizeof(text), "%d Pending Sync", pending);
    return text;
}
