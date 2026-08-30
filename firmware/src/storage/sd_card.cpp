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
    f.close();
    return written == len;
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
