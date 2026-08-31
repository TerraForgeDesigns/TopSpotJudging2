#include "photo_state.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <cstring>

#include "sd_card.h"

namespace storage {

namespace {

constexpr const char* TRANSFER_STATE_PATH = "/photo_transfer_state.json";
constexpr size_t MAX_TRANSFER_STATE_SIZE = 16 * 1024;  // generous for hundreds of filenames

// Parses "{entryNumber}_{type}.jpg" into its two parts. Returns false for
// anything that doesn't match this exact shape (e.g. settings.txt,
// show.json — every other file this firmware writes to the SD root) so
// callers can filter the directory listing down to real photos only.
bool parsePhotoFilename(const char* filename, char* entryOut, size_t entrySize, char* typeOut, size_t typeSize) {
    const char* dot = strstr(filename, ".jpg");
    if (dot == nullptr || dot[4] != '\0') return false;  // must end in exactly ".jpg"

    const char* underscore = strchr(filename, '_');
    if (underscore == nullptr || underscore >= dot) return false;

    size_t entryLen = static_cast<size_t>(underscore - filename);
    size_t typeLen = static_cast<size_t>(dot - underscore - 1);
    if (entryLen == 0 || entryLen >= entrySize || typeLen == 0 || typeLen >= typeSize) return false;

    strncpy(entryOut, filename, entryLen);
    entryOut[entryLen] = '\0';
    strncpy(typeOut, underscore + 1, typeLen);
    typeOut[typeLen] = '\0';

    return strcmp(typeOut, "car") == 0 || strcmp(typeOut, "sheet") == 0;
}

bool isTransferred(JsonArrayConst transferredArr, const char* filename) {
    for (JsonVariantConst v : transferredArr) {
        const char* s = v.as<const char*>();
        if (s != nullptr && strcmp(s, filename) == 0) return true;
    }
    return false;
}

// Loads the whole manifest document — `doc["transferred"]` is the
// filename array every caller actually wants. Leaves `doc` as an empty
// object (so `doc["transferred"]` reads as a null/empty array, per
// ArduinoJson's usual "missing key" behavior) if the manifest doesn't
// exist yet or fails to parse — no photos transferred yet is the normal
// pre-first-transfer state, never an error.
void loadManifest(DynamicJsonDocument& doc) {
    if (!isMounted() || !fileExists(TRANSFER_STATE_PATH)) return;
    uint8_t buf[MAX_TRANSFER_STATE_SIZE];
    int n = readFile(TRANSFER_STATE_PATH, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    if (deserializeJson(doc, buf, n)) doc.clear();
}

// The one real SD-directory walk every function in this file is built
// on. `out`/`maxOut` are optional (out may be null, maxOut 0) — a
// count-only caller (countPhotos/countUntransferred/allTransferred) never
// needs to materialize a PhotoRecord array at all, just walk the
// directory and tally. `*totalOut`/`*untransferredOut` are always the
// TRUE totals regardless of `maxOut`, the same bounded-but-honest-count
// shape storage::vehicle_db's findMakes()/findModels() already use.
void scanPhotos(PhotoRecord* out, int maxOut, int* totalOut, int* untransferredOut) {
    *totalOut = 0;
    *untransferredOut = 0;
    if (!isMounted()) return;
    File dir = SD.open("/");
    if (!dir || !dir.isDirectory()) return;

    DynamicJsonDocument manifest(MAX_TRANSFER_STATE_SIZE * 2);
    loadManifest(manifest);
    JsonArrayConst transferredArr = manifest["transferred"].as<JsonArrayConst>();

    File entry = dir.openNextFile();
    while (entry) {
        String name = entry.name();
        uint32_t size = entry.size();
        bool isDir = entry.isDirectory();
        entry.close();

        char entryNum[8];
        char type[8];
        if (!isDir && parsePhotoFilename(name.c_str(), entryNum, sizeof(entryNum), type, sizeof(type))) {
            bool transferred = isTransferred(transferredArr, name.c_str());
            (*totalOut)++;
            if (!transferred) (*untransferredOut)++;

            if (out != nullptr && *totalOut <= maxOut) {
                PhotoRecord& rec = out[*totalOut - 1];
                strncpy(rec.entryNumber, entryNum, sizeof(rec.entryNumber) - 1);
                strncpy(rec.type, type, sizeof(rec.type) - 1);
                strncpy(rec.filename, name.c_str(), sizeof(rec.filename) - 1);
                rec.sizeBytes = size;
                rec.transferred = transferred;
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

}  // namespace

int listPhotos(PhotoRecord* out, int maxOut) {
    int total = 0, untransferred = 0;
    scanPhotos(out, maxOut, &total, &untransferred);
    return total < maxOut ? total : maxOut;
}

int countPhotos() {
    int total = 0, untransferred = 0;
    scanPhotos(nullptr, 0, &total, &untransferred);
    return total;
}

int countUntransferred() {
    int total = 0, untransferred = 0;
    scanPhotos(nullptr, 0, &total, &untransferred);
    return untransferred;
}

bool allTransferred() {
    int total = 0, untransferred = 0;
    scanPhotos(nullptr, 0, &total, &untransferred);
    // Nothing on the card is deliberately NOT "all transferred" — see clearAllPhotos()'s own doc.
    return total > 0 && untransferred == 0;
}

void markTransferred(const char* filename) {
    DynamicJsonDocument doc(MAX_TRANSFER_STATE_SIZE * 2);
    loadManifest(doc);
    JsonArray arr = doc["transferred"].is<JsonArray>() ? doc["transferred"].as<JsonArray>()
                                                        : doc.createNestedArray("transferred");

    for (JsonVariantConst v : arr) {
        const char* s = v.as<const char*>();
        if (s != nullptr && strcmp(s, filename) == 0) return;  // already recorded
    }
    arr.add(filename);

    char buf[MAX_TRANSFER_STATE_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return;
    writeFileAtomic(TRANSFER_STATE_PATH, reinterpret_cast<const uint8_t*>(buf), len);
}

bool clearAllPhotos() {
    if (!allTransferred()) return false;

    int total = countPhotos();
    if (total == 0) return true;  // allTransferred() already requires total>0, but stay defensive
    auto* photos = new PhotoRecord[total];
    int n = listPhotos(photos, total);
    for (int i = 0; i < n; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/%s", photos[i].filename);
        deleteFile(path);
    }
    delete[] photos;
    deleteFile(TRANSFER_STATE_PATH);
    return true;
}

}  // namespace storage
