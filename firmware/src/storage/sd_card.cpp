#include "sd_card.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "pins.h"

namespace storage {

namespace {
// Dedicated second hardware SPI peripheral for SD — HSPI, deliberately
// distinct from the Arducam Mega's hardcoded use of the global `SPI`
// (FSPI) object. See sd_card.h's header comment.
SPIClass g_sdSpi(HSPI);
bool g_mounted = false;
}  // namespace

bool begin() {
    g_sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    g_mounted = SD.begin(PIN_SD_CS, g_sdSpi, 20000000, "/sd");
    return g_mounted;
}

bool isMounted() { return g_mounted; }

SdInfo getInfo() {
    SdInfo info;
    info.mounted = g_mounted;
    if (!g_mounted) return info;
    info.totalBytes = SD.totalBytes();
    info.usedBytes = SD.usedBytes();
    return info;
}

bool writeFile(const char* path, const uint8_t* data, size_t len) {
    if (!g_mounted) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t written = f.write(data, len);
    f.flush();
    f.close();
    return written == len;
}

namespace {
String tmpPathFor(const char* path) {
    String tmp(path);
    tmp += ".tmp";
    return tmp;
}
}  // namespace

bool writeFileAtomic(const char* path, const uint8_t* data, size_t len) {
    if (!g_mounted) return false;
    String tmpPath = tmpPathFor(path);

    // A previous crash mid-write may have left a stale .tmp here —
    // never trust it, always start this attempt from a clean slate.
    if (SD.exists(tmpPath)) SD.remove(tmpPath);

    File f = SD.open(tmpPath, FILE_WRITE);
    if (!f) return false;
    size_t written = f.write(data, len);
    f.flush();
    f.close();
    if (written != len) {
        SD.remove(tmpPath);
        return false;
    }

    // Verify by reopening and checking size — cheap, and catches a
    // flush that silently didn't fully land (seen on some SD cards
    // under low battery / marginal contact) before it ever reaches the
    // real filename.
    File check = SD.open(tmpPath, FILE_READ);
    if (!check || check.size() != len) {
        if (check) check.close();
        SD.remove(tmpPath);
        return false;
    }
    check.close();

    if (SD.exists(path)) SD.remove(path);  // see sd_card.h's header comment on this narrow window
    bool renamed = SD.rename(tmpPath, path);
    if (!renamed) {
        // Leaves the verified-good data sitting in the .tmp file rather
        // than losing it outright — not the desired end state, but
        // strictly better than silently discarding a write that
        // otherwise succeeded. The caller still sees this as failure.
        return false;
    }
    return true;
}

bool ensureDir(const char* path) {
    if (!g_mounted) return false;
    if (SD.exists(path)) return true;

    // Walk each '/'-separated segment, creating it if missing — matches
    // this function's header comment: SD.mkdir() on this library does
    // NOT nest-create, so a multi-level path (e.g. "/photos/unmatched")
    // needs "/photos" to exist before "/photos/unmatched" can be created.
    String p(path);
    for (int i = 1; i < static_cast<int>(p.length()); i++) {
        if (p[i] == '/') {
            String segment = p.substring(0, i);
            if (!SD.exists(segment) && !SD.mkdir(segment)) return false;
        }
    }
    return SD.mkdir(p);
}

int readFile(const char* path, uint8_t* outBuf, size_t maxLen) {
    if (!g_mounted) return -1;
    File f = SD.open(path, FILE_READ);
    if (!f) return -1;
    size_t n = f.read(outBuf, maxLen);
    f.close();
    return static_cast<int>(n);
}

bool fileExists(const char* path) {
    if (!g_mounted) return false;
    return SD.exists(path);
}

bool deleteFile(const char* path) {
    if (!g_mounted) return false;
    return SD.remove(path);
}

}  // namespace storage
