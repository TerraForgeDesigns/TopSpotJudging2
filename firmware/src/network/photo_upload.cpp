#include "photo_upload.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "diag/log.h"
#include "storage/photo_state.h"
#include "storage/sd_card.h"
#include "storage/settings.h"
#include "ui/components/toast.h"

namespace network::photo_upload {

namespace {

constexpr int MAX_BATCH = 500;  // generous — a full show is ~250 cars x 2 photos
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;  // photo bodies are much larger than the F5 sync JSON — more generous
constexpr uint32_t POLL_TIMER_MS = 150;
constexpr size_t MAX_PHOTO_FILE_SIZE = 2 * 1024 * 1024;  // generous ceiling for one JPEG — see camera.h's CAPTURE_MODE_PHOTO
constexpr const char* BOUNDARY = "----TopSpotJudgingBoundary7MA4YWxkTrZu0gW";

// Heap-allocated (not a static MAX_BATCH-sized array) — a fixed 500-entry
// static reservation costs ~24KB of this chip's 320KB SRAM whether or not
// a transfer is even running; see photo_transfer_screen.cpp's matching
// comment and DECISIONS.md for the link-time DRAM overflow this exact
// mistake caused during F6's own build verification.
storage::PhotoRecord* g_photos = nullptr;
int g_photoCount = 0;
int g_currentIndex = 0;  // index into g_photos of the photo currently in flight (or about to start)
bool g_running = false;

lv_timer_t* g_pollTimer = nullptr;

struct Attempt {
    // Input — set by the main thread (buildAndSendCurrent()) before the task starts.
    char ssid[32] = "";
    char password[64] = "";
    char url[160] = "";
    char* body = nullptr;
    size_t bodyLen = 0;

    // Output — set by uploadTask() (background).
    volatile bool done = false;
    bool ok = false;
};

Attempt g_attempt;

void stopBatch(const char* reason) {
    diag::log("[photo_upload] stopped: %s", reason);
    g_running = false;
    g_currentIndex = 0;
    g_photoCount = 0;
    delete[] g_photos;
    g_photos = nullptr;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);  // CONTEXT.md: radio off outside a sync window
}

// ---- background task: HTTP POST only, nothing else ---------------------

void uploadTask(void* /*unused*/) {
    g_attempt.ok = false;

    // No storage reads here — ssid/password/url/body are all already
    // populated by the main thread before this task was created (see
    // buildAndSendCurrent()). This function touches no storage or LVGL
    // at all, same discipline as wifi_sync.cpp.
    //
    // Deliberately stays associated between photos (unlike wifi_sync.cpp,
    // which disconnects after every attempt) — this is one continuous,
    // judge-watched batch, potentially hundreds of photos, and paying a
    // full WiFi handshake per photo would make "Sending photo 34 of 88"
    // needlessly slow. stopBatch() (main thread, called once the whole
    // batch finishes or fails) is what actually disconnects and turns
    // the radio off.
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_attempt.ssid, g_attempt.password);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
            delay(100);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;
        if (http.begin(client, g_attempt.url)) {
            char contentType[96];
            snprintf(contentType, sizeof(contentType), "multipart/form-data; boundary=%s", BOUNDARY);
            http.addHeader("Content-Type", contentType);
            http.setTimeout(HTTP_TIMEOUT_MS);
            int code = http.POST(reinterpret_cast<uint8_t*>(g_attempt.body), g_attempt.bodyLen);
            g_attempt.ok = (code == 200);
            http.end();
        }
    }

    heap_caps_free(g_attempt.body);
    g_attempt.body = nullptr;
    g_attempt.done = true;
    vTaskDelete(nullptr);
}

// ---- main thread: storage reads, multipart body construction -----------

// Builds the full multipart/form-data body for g_photos[g_currentIndex]
// in one PSRAM buffer (prefix fields + the raw JPEG bytes + the closing
// boundary) and launches the background task to send it. Storage read
// (the photo file itself) happens here, not in the task — see this
// module's header comment for why.
bool buildAndSendCurrent() {
    const storage::PhotoRecord& photo = g_photos[g_currentIndex];

    char path[32];
    snprintf(path, sizeof(path), "/%s", photo.filename);
    auto* fileBuf = static_cast<uint8_t*>(heap_caps_malloc(MAX_PHOTO_FILE_SIZE, MALLOC_CAP_SPIRAM));
    if (fileBuf == nullptr) return false;
    int fileLen = storage::readFile(path, fileBuf, MAX_PHOTO_FILE_SIZE);
    if (fileLen <= 0) {
        heap_caps_free(fileBuf);
        return false;
    }

    char prefix[512];
    int prefixLen = snprintf(prefix, sizeof(prefix),
                              "--%s\r\n"
                              "Content-Disposition: form-data; name=\"entry_number\"\r\n\r\n%s\r\n"
                              "--%s\r\n"
                              "Content-Disposition: form-data; name=\"photo_type\"\r\n\r\n%s\r\n"
                              "--%s\r\n"
                              "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                              "Content-Type: image/jpeg\r\n\r\n",
                              BOUNDARY, photo.entryNumber, BOUNDARY, photo.type, BOUNDARY, photo.filename);

    char suffix[64];
    int suffixLen = snprintf(suffix, sizeof(suffix), "\r\n--%s--\r\n", BOUNDARY);

    size_t totalLen = static_cast<size_t>(prefixLen) + static_cast<size_t>(fileLen) + static_cast<size_t>(suffixLen);
    auto* body = static_cast<char*>(heap_caps_malloc(totalLen, MALLOC_CAP_SPIRAM));
    if (body == nullptr) {
        heap_caps_free(fileBuf);
        return false;
    }
    memcpy(body, prefix, prefixLen);
    memcpy(body + prefixLen, fileBuf, fileLen);
    memcpy(body + prefixLen + fileLen, suffix, suffixLen);
    heap_caps_free(fileBuf);

    storage::Settings settings;
    storage::loadSettings(&settings);
    strncpy(g_attempt.ssid, settings.wifiSsid, sizeof(g_attempt.ssid) - 1);
    strncpy(g_attempt.password, settings.wifiPassword, sizeof(g_attempt.password) - 1);
    snprintf(g_attempt.url, sizeof(g_attempt.url), "http://%s/api/v1/photos/upload", settings.homeBaseAddress);
    g_attempt.body = body;
    g_attempt.bodyLen = totalLen;
    g_attempt.done = false;

    xTaskCreatePinnedToCore(uploadTask, "photo_upload", 8192, nullptr, 1, nullptr, 1);
    return true;
}

void advance() {
    g_currentIndex++;
    if (g_currentIndex >= g_photoCount) {
        diag::log("[photo_upload] transfer complete: %d photo(s) sent", g_photoCount);
        stopBatch("complete");
        return;
    }
    if (!buildAndSendCurrent()) {
        stopBatch("could not build the next photo's request");
    }
}

void pollTimerCb(lv_timer_t* /*timer*/) {
    if (!g_running || !g_attempt.done) return;
    g_attempt.done = false;

    if (g_attempt.ok) {
        storage::markTransferred(g_photos[g_currentIndex].filename);
        advance();
    } else {
        ui::components::showToast("Home Base Not Connected", ui::components::ToastSeverity::Error);
        stopBatch("upload failed — queue left untouched, resumes from here next time");
    }
}

}  // namespace

void startTransfer() {
    if (g_running) return;

    // Bounded to MAX_BATCH per tap (not the card's true total, which
    // could exceed it on a very large show) — the transfer is resumable
    // by construction (see this module's header comment), so a judge
    // simply taps "Send Photos over Wi-Fi" again once this batch
    // finishes if more remain; never data-lossy, just possibly two taps.
    auto* all = new storage::PhotoRecord[MAX_BATCH];
    int allCount = storage::listPhotos(all, MAX_BATCH);

    g_photos = new storage::PhotoRecord[allCount];
    g_photoCount = 0;
    for (int i = 0; i < allCount; i++) {
        if (!all[i].transferred) g_photos[g_photoCount++] = all[i];
    }
    delete[] all;
    g_currentIndex = 0;

    if (g_photoCount == 0) {
        delete[] g_photos;
        g_photos = nullptr;
        return;
    }

    if (g_pollTimer == nullptr) g_pollTimer = lv_timer_create(pollTimerCb, POLL_TIMER_MS, nullptr);

    g_running = true;
    if (!buildAndSendCurrent()) stopBatch("could not build the first photo's request");
}

bool isRunning() { return g_running; }
int currentIndex() { return g_running ? g_currentIndex + 1 : 0; }
int total() { return g_running ? g_photoCount : 0; }

}  // namespace network::photo_upload
