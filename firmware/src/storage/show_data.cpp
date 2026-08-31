#include "show_data.h"

#include <ArduinoJson.h>
#include <cstring>

#include "sd_card.h"

namespace storage {

namespace {

constexpr const char* SHOW_INFO_PATH = "/show.json";
constexpr const char* ENTRIES_PATH = "/entries.json";

// Entries can run into the hundreds (CONTEXT.md: 100-400 cars) — read the
// whole file into a heap buffer sized to what's actually on disk, rather
// than a fixed guess. Capped generously; a file bigger than this is
// treated as unreadable rather than risking an unbounded allocation from
// a corrupt/foreign file.
constexpr size_t MAX_ENTRIES_FILE_SIZE = 512 * 1024;
constexpr size_t MAX_SHOW_INFO_FILE_SIZE = 16 * 1024;

void copyStr(char* dst, size_t dstSize, JsonVariantConst v) {
    const char* s = v.as<const char*>();
    if (s == nullptr) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, s, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

// Parses the leading "YYYY" off an ISO "YYYY-MM-DD" string — deliberately
// not a general date parser (no library, no locale handling needed for
// four ASCII digits). Returns 0 for anything that isn't exactly that
// shape, which ShowInfo::eventYear treats the same as "no show synced
// yet."
int parseYear(const char* isoDate) {
    if (isoDate == nullptr || strlen(isoDate) < 4) return 0;
    for (int i = 0; i < 4; i++) {
        if (isoDate[i] < '0' || isoDate[i] > '9') return 0;
    }
    return (isoDate[0] - '0') * 1000 + (isoDate[1] - '0') * 100 + (isoDate[2] - '0') * 10 + (isoDate[3] - '0');
}

// Reads `path` into a heap buffer, null-terminated. Returns nullptr (and
// leaves *outLen untouched) if the file is missing, unreadable, or larger
// than `maxSize` — caller treats that as "no data yet," never a crash.
uint8_t* readWholeFile(const char* path, size_t maxSize, size_t* outLen) {
    if (!isMounted() || !fileExists(path)) return nullptr;
    // Read in one shot up to maxSize; readFile() itself caps at the
    // buffer size passed in, so an oversized file is silently truncated
    // rather than overflowing — detected below via a full-buffer read.
    uint8_t* buf = static_cast<uint8_t*>(malloc(maxSize + 1));
    if (buf == nullptr) return nullptr;
    int n = readFile(path, buf, maxSize);
    if (n <= 0 || static_cast<size_t>(n) >= maxSize) {  // >= maxSize likely means truncation, not a real fit
        free(buf);
        return nullptr;
    }
    buf[n] = '\0';
    *outLen = static_cast<size_t>(n);
    return buf;
}

}  // namespace

bool loadShowInfo(ShowInfo* out) {
    *out = ShowInfo{};
    size_t len = 0;
    uint8_t* buf = readWholeFile(SHOW_INFO_PATH, MAX_SHOW_INFO_FILE_SIZE, &len);
    if (buf == nullptr) return true;  // no show cached yet is normal before the first sync — defaults stand

    DynamicJsonDocument doc(MAX_SHOW_INFO_FILE_SIZE * 4);  // JSON parsing overhead vs. raw bytes — generous, PSRAM is cheap
    DeserializationError err = deserializeJson(doc, buf, len);
    free(buf);
    if (err) return true;  // malformed cache — never blocks boot, just judges with stale/no config until next sync

    out->showId = doc["show_id"] | 0;
    copyStr(out->showName, sizeof(out->showName), doc["show_name"]);
    copyStr(out->eventDate, sizeof(out->eventDate), doc["event_date"]);
    out->eventYear = parseYear(out->eventDate);
    out->scoreRangeMax = doc["score_range_max"] | 5;
    out->maxScore = doc["max_score"] | 0;
    out->overallImpressionEnabled = doc["overall_impression_enabled"] | false;

    JsonArrayConst cats = doc["categories"].as<JsonArrayConst>();
    out->categoryCount = 0;
    for (JsonVariantConst c : cats) {
        if (out->categoryCount >= MAX_CATEGORIES) break;
        Category& cat = out->categories[out->categoryCount++];
        cat.id = c["id"] | 0;
        cat.sortOrder = c["sort_order"] | 0;
        copyStr(cat.name, sizeof(cat.name), c["name"]);
    }

    JsonArrayConst awards = doc["judge_chosen_awards"].as<JsonArrayConst>();
    out->awardCount = 0;
    for (JsonVariantConst a : awards) {
        if (out->awardCount >= MAX_AWARDS) break;
        JudgeChosenAward& award = out->awards[out->awardCount++];
        award.id = a["id"] | 0;
        copyStr(award.name, sizeof(award.name), a["name"]);
    }
    return true;
}

bool saveShowInfo(const ShowInfo& info) {
    DynamicJsonDocument doc(MAX_SHOW_INFO_FILE_SIZE * 4);
    doc["show_id"] = info.showId;
    doc["show_name"] = info.showName;
    doc["event_date"] = info.eventDate;
    doc["score_range_max"] = info.scoreRangeMax;
    doc["max_score"] = info.maxScore;
    doc["overall_impression_enabled"] = info.overallImpressionEnabled;

    JsonArray cats = doc.createNestedArray("categories");
    for (int i = 0; i < info.categoryCount; i++) {
        JsonObject c = cats.createNestedObject();
        c["id"] = info.categories[i].id;
        c["name"] = info.categories[i].name;
        c["sort_order"] = info.categories[i].sortOrder;
    }

    JsonArray awards = doc.createNestedArray("judge_chosen_awards");
    for (int i = 0; i < info.awardCount; i++) {
        JsonObject a = awards.createNestedObject();
        a["id"] = info.awards[i].id;
        a["name"] = info.awards[i].name;
    }

    char buf[MAX_SHOW_INFO_FILE_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    return writeFileAtomic(SHOW_INFO_PATH, reinterpret_cast<const uint8_t*>(buf), len);
}

bool findEntry(const char* entryNumber, Entry* out) {
    size_t len = 0;
    uint8_t* buf = readWholeFile(ENTRIES_PATH, MAX_ENTRIES_FILE_SIZE, &len);
    if (buf == nullptr) return false;  // no cache at all yet — every entry number is "not in the list yet"

    DynamicJsonDocument doc(len * 3 + 1024);  // JSON parsing overhead over the raw file size
    DeserializationError err = deserializeJson(doc, buf, len);
    free(buf);
    if (err) return false;

    for (JsonVariantConst e : doc.as<JsonArrayConst>()) {
        const char* num = e["entry_number"] | "";
        if (strcmp(num, entryNumber) == 0) {
            copyStr(out->entryNumber, sizeof(out->entryNumber), e["entry_number"]);
            copyStr(out->participant, sizeof(out->participant), e["participant"]);
            copyStr(out->year, sizeof(out->year), e["year"]);
            copyStr(out->make, sizeof(out->make), e["make"]);
            copyStr(out->model, sizeof(out->model), e["model"]);
            copyStr(out->vehicleType, sizeof(out->vehicleType), e["vehicle_type"]);
            return true;
        }
    }
    return false;
}

bool saveEntries(const Entry* entries, int count) {
    // Sized per-entry rather than one big fixed guess — 400 cars x a
    // fixed small doc would either waste PSRAM or risk overflow; this
    // scales with what's actually being written.
    DynamicJsonDocument doc(static_cast<size_t>(count) * 320 + 1024);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject e = arr.createNestedObject();
        e["entry_number"] = entries[i].entryNumber;
        e["participant"] = entries[i].participant;
        e["year"] = entries[i].year;
        e["make"] = entries[i].make;
        e["model"] = entries[i].model;
        e["vehicle_type"] = entries[i].vehicleType;
    }

    size_t needed = measureJson(doc) + 1;
    char* buf = static_cast<char*>(malloc(needed));
    if (buf == nullptr) return false;
    size_t len = serializeJson(doc, buf, needed);
    bool ok = len > 0 && writeFileAtomic(ENTRIES_PATH, reinterpret_cast<const uint8_t*>(buf), len);
    free(buf);
    return ok;
}

}  // namespace storage
