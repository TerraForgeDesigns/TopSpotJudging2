// Bring-up test: onboard microSD, in isolation (no display, no camera —
// confirms the SD SPI bus works completely on its own before anything
// else touches SPI at all).
//
// WHAT TO PHYSICALLY VERIFY:
//   1. Open the Serial monitor at 115200 baud.
//   2. Confirm you see "MOUNT: OK", then "WRITE: OK", "READ: OK, content
//      matches", and a plausible free-space number (should roughly match
//      your card's real capacity minus whatever's already on it).
//   3. If MOUNT fails: check the card is inserted and formatted FAT32,
//      and re-check the SD_CS/MOSI/SCK/MISO solder joints against
//      pins.h (10/11/12/13) — this is the onboard slot, not the camera.
#include <Arduino.h>

#include "storage/sd_card.h"

namespace {
const char* kTestPath = "/bringup_test.txt";
const char* kTestContent = "Top Spot Judging SD bring-up test\n";
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[bringup-sd] starting");

    if (!storage::begin()) {
        Serial.println("[bringup-sd] MOUNT: FAILED -- no card, bad wiring, or unformatted card");
        return;
    }
    Serial.println("[bringup-sd] MOUNT: OK");

    size_t len = strlen(kTestContent);
    bool wrote = storage::writeFile(kTestPath, reinterpret_cast<const uint8_t*>(kTestContent), len);
    Serial.printf("[bringup-sd] WRITE: %s\n", wrote ? "OK" : "FAILED");

    uint8_t readBuf[128] = {0};
    int readLen = storage::readFile(kTestPath, readBuf, sizeof(readBuf) - 1);
    bool matches = readLen == static_cast<int>(len) && memcmp(readBuf, kTestContent, len) == 0;
    Serial.printf("[bringup-sd] READ: %s (%d bytes) -- content %s\n", readLen >= 0 ? "OK" : "FAILED", readLen,
                  matches ? "matches" : "DOES NOT MATCH");

    storage::SdInfo info = storage::getInfo();
    Serial.printf("[bringup-sd] capacity: total=%llu MB, used=%llu MB, free=%llu MB\n",
                  info.totalBytes / (1024ULL * 1024), info.usedBytes / (1024ULL * 1024),
                  info.freeBytes() / (1024ULL * 1024));

    storage::deleteFile(kTestPath);
    Serial.println("[bringup-sd] test file cleaned up -- done");
}

void loop() { delay(1000); }
