#include "vehicle_recents.h"

#include <ArduinoJson.h>
#include <cstring>

#include "sd_card.h"
#include "show_data.h"

namespace storage {

namespace {

constexpr const char* RECENTS_PATH = "/vehicle_recents.json";
constexpr size_t MAX_RECENTS_FILE_SIZE = 8192;
constexpr int MAX_RECENT_MAKES = 20;
constexpr int MAX_RECENT_MODELS_PER_MAKE = 10;

// Loads the recents file IF it matches the current show's id — a
// mismatch (or no file, no SD, no show cached) means "nothing recorded
// for THIS show yet," so the caller treats it exactly like an empty doc
// rather than stale data from a prior or renamed show. Returns false in
// every one of those cases; `doc` is left however deserializeJson leaves
// a failed/untouched parse, which the caller never reads from when this
// returns false.
bool loadForCurrentShow(DynamicJsonDocument& doc) {
    if (!isMounted() || !fileExists(RECENTS_PATH)) return false;

    ShowInfo show;
    loadShowInfo(&show);
    if (show.showId == 0) return false;  // no show synced yet — nothing can be "this show's" recents

    uint8_t buf[MAX_RECENTS_FILE_SIZE];
    int n = readFile(RECENTS_PATH, buf, sizeof(buf) - 1);
    if (n <= 0) return false;
    buf[n] = '\0';

    if (deserializeJson(doc, buf, n)) return false;
    int storedShowId = doc["show_id"] | 0;
    return storedShowId == show.showId;
}

bool save(const DynamicJsonDocument& doc) {
    char buf[MAX_RECENTS_FILE_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    return writeFileAtomic(RECENTS_PATH, reinterpret_cast<const uint8_t*>(buf), len);
}

// Moves `value` to the front of `arr` (an array of strings), inserting it
// if absent, de-duplicating case-insensitively, and capping length at
// `maxLen` by dropping the oldest (last) entries — the standard "most
// recently used" list shape.
void bumpToFront(JsonArray arr, const char* value, int maxLen) {
    // Collect existing values (skipping a case-insensitive match on
    // `value`, which is about to be re-inserted at the front), then
    // rebuild the array in one pass — ArduinoJson's JsonArray has no
    // cheap "move element" primitive, and these lists are tiny (<=20/10
    // entries) so a full rebuild is negligible cost either way.
    char kept[24][48];  // MAX_RECENT_MAKES is the largest cap this is ever called with
    int keptCount = 0;
    for (JsonVariantConst v : arr) {
        const char* s = v.as<const char*>();
        if (s == nullptr || strcasecmp(s, value) == 0) continue;
        if (keptCount < 23) {
            strncpy(kept[keptCount], s, sizeof(kept[keptCount]) - 1);
            kept[keptCount][sizeof(kept[keptCount]) - 1] = '\0';
            keptCount++;
        }
    }
    arr.clear();
    arr.add(value);
    for (int i = 0; i < keptCount && i < maxLen - 1; i++) arr.add(kept[i]);
}

}  // namespace

void recordMakeUsed(const char* make) {
    if (make == nullptr || make[0] == '\0' || !isMounted()) return;
    ShowInfo show;
    loadShowInfo(&show);
    if (show.showId == 0) return;  // no show synced yet — nothing to scope this recording to

    DynamicJsonDocument doc(MAX_RECENTS_FILE_SIZE * 2);
    if (!loadForCurrentShow(doc)) {
        doc.clear();
        doc["show_id"] = show.showId;
    }
    JsonArray makes = doc["makes"].is<JsonArray>() ? doc["makes"].as<JsonArray>() : doc.createNestedArray("makes");
    bumpToFront(makes, make, MAX_RECENT_MAKES);
    save(doc);
}

void recordModelUsed(const char* make, const char* model) {
    if (make == nullptr || model == nullptr || make[0] == '\0' || model[0] == '\0' || !isMounted()) return;
    ShowInfo show;
    loadShowInfo(&show);
    if (show.showId == 0) return;

    DynamicJsonDocument doc(MAX_RECENTS_FILE_SIZE * 2);
    if (!loadForCurrentShow(doc)) {
        doc.clear();
        doc["show_id"] = show.showId;
    }
    JsonObject models = doc["models"].is<JsonObject>() ? doc["models"].as<JsonObject>() : doc.createNestedObject("models");
    JsonArray forMake = models[make].is<JsonArray>() ? models[make].as<JsonArray>() : models.createNestedArray(make);
    bumpToFront(forMake, model, MAX_RECENT_MODELS_PER_MAKE);
    save(doc);
}

int getRecentMakes(char out[][48], int maxOut) {
    DynamicJsonDocument doc(MAX_RECENTS_FILE_SIZE * 2);
    if (!loadForCurrentShow(doc)) return 0;

    int n = 0;
    for (JsonVariantConst v : doc["makes"].as<JsonArrayConst>()) {
        if (n >= maxOut) break;
        const char* s = v.as<const char*>();
        if (s == nullptr) continue;
        strncpy(out[n], s, 47);
        out[n][47] = '\0';
        n++;
    }
    return n;
}

int getRecentModels(const char* make, char out[][48], int maxOut) {
    DynamicJsonDocument doc(MAX_RECENTS_FILE_SIZE * 2);
    if (!loadForCurrentShow(doc)) return 0;

    int n = 0;
    for (JsonVariantConst v : doc["models"][make].as<JsonArrayConst>()) {
        if (n >= maxOut) break;
        const char* s = v.as<const char*>();
        if (s == nullptr) continue;
        strncpy(out[n], s, 47);
        out[n][47] = '\0';
        n++;
    }
    return n;
}

}  // namespace storage
