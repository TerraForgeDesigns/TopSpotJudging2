#include "drafts.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

#include "sd_card.h"

namespace storage {

namespace {

constexpr const char* DRAFTS_DIR = "/drafts";
constexpr size_t MAX_DRAFT_FILE_SIZE = 4096;

bool buildPath(const char* entryNumber, char* out, size_t outSize) {
    int n = snprintf(out, outSize, "%s/%s.json", DRAFTS_DIR, entryNumber);
    return n > 0 && static_cast<size_t>(n) < outSize;
}

const char* stepName(DraftStep step) {
    switch (step) {
        case DraftStep::VehicleDetails: return "vehicle_details";
        case DraftStep::Judging: return "judging";
        case DraftStep::Nominations: return "nominations";
        case DraftStep::Photos: return "photos";
        case DraftStep::Review: return "review";
    }
    return "vehicle_details";
}

DraftStep parseStep(const char* s) {
    if (strcmp(s, "judging") == 0) return DraftStep::Judging;
    if (strcmp(s, "nominations") == 0) return DraftStep::Nominations;
    if (strcmp(s, "photos") == 0) return DraftStep::Photos;
    if (strcmp(s, "review") == 0) return DraftStep::Review;
    return DraftStep::VehicleDetails;
}

void copyStr(char* dst, size_t dstSize, JsonVariantConst v) {
    const char* s = v.as<const char*>();
    if (s == nullptr) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, s, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

}  // namespace

bool saveDraft(const DraftCar& draft) {
    if (!ensureDir(DRAFTS_DIR)) return false;
    char path[64];
    if (!buildPath(draft.entryNumber, path, sizeof(path))) return false;

    StaticJsonDocument<MAX_DRAFT_FILE_SIZE * 2> doc;
    doc["entry_number"] = draft.entryNumber;
    doc["furthest_step"] = stepName(draft.furthestStep);
    doc["not_in_roster_yet"] = draft.notInRosterYet;
    doc["participant"] = draft.participant;
    doc["year"] = draft.year;
    doc["make"] = draft.make;
    doc["model"] = draft.model;
    doc["vehicle_type"] = draft.vehicleType;
    doc["make_manually_entered"] = draft.makeManuallyEntered;
    doc["model_manually_entered"] = draft.modelManuallyEntered;
    doc["score_range_max"] = draft.scoreRangeMax;

    JsonArray scores = doc.createNestedArray("scores");
    for (int i = 0; i < draft.scoreCount; i++) {
        JsonObject s = scores.createNestedObject();
        s["category_id"] = draft.scores[i].categoryId;
        s["points"] = draft.scores[i].points;
    }
    doc["has_overall_impression"] = draft.hasOverallImpression;
    doc["overall_impression"] = draft.overallImpression;

    JsonArray noms = doc.createNestedArray("nominations");
    for (int i = 0; i < draft.nominationCount; i++) noms.add(draft.nominations[i]);

    doc["car_photo_saved"] = draft.carPhotoSaved;
    doc["sheet_photo_saved"] = draft.sheetPhotoSaved;

    char buf[MAX_DRAFT_FILE_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;
    return writeFileAtomic(path, reinterpret_cast<const uint8_t*>(buf), len);
}

bool loadDraft(const char* entryNumber, DraftCar* out) {
    *out = DraftCar{};
    strncpy(out->entryNumber, entryNumber, sizeof(out->entryNumber) - 1);

    char path[64];
    if (!buildPath(entryNumber, path, sizeof(path))) return false;
    if (!isMounted() || !fileExists(path)) return false;

    uint8_t buf[MAX_DRAFT_FILE_SIZE];
    int n = readFile(path, buf, sizeof(buf) - 1);
    if (n <= 0) return false;
    buf[n] = '\0';

    StaticJsonDocument<MAX_DRAFT_FILE_SIZE * 2> doc;
    if (deserializeJson(doc, buf, n)) return false;

    out->furthestStep = parseStep(doc["furthest_step"] | "vehicle_details");
    out->notInRosterYet = doc["not_in_roster_yet"] | false;
    copyStr(out->participant, sizeof(out->participant), doc["participant"]);
    copyStr(out->year, sizeof(out->year), doc["year"]);
    copyStr(out->make, sizeof(out->make), doc["make"]);
    copyStr(out->model, sizeof(out->model), doc["model"]);
    copyStr(out->vehicleType, sizeof(out->vehicleType), doc["vehicle_type"]);
    out->makeManuallyEntered = doc["make_manually_entered"] | false;
    out->modelManuallyEntered = doc["model_manually_entered"] | false;
    out->scoreRangeMax = doc["score_range_max"] | 5;

    out->scoreCount = 0;
    for (JsonVariantConst s : doc["scores"].as<JsonArrayConst>()) {
        if (out->scoreCount >= MAX_QUEUED_SCORES) break;
        out->scores[out->scoreCount].categoryId = s["category_id"] | 0;
        out->scores[out->scoreCount].points = s["points"] | 0;
        out->scoreCount++;
    }
    out->hasOverallImpression = doc["has_overall_impression"] | false;
    out->overallImpression = doc["overall_impression"] | 0;

    out->nominationCount = 0;
    for (JsonVariantConst id : doc["nominations"].as<JsonArrayConst>()) {
        if (out->nominationCount >= MAX_QUEUED_NOMINATIONS) break;
        out->nominations[out->nominationCount++] = id.as<int>();
    }

    out->carPhotoSaved = doc["car_photo_saved"] | false;
    out->sheetPhotoSaved = doc["sheet_photo_saved"] | false;
    return true;
}

bool hasDraft(const char* entryNumber) {
    char path[64];
    if (!buildPath(entryNumber, path, sizeof(path))) return false;
    return isMounted() && fileExists(path);
}

bool deleteDraft(const char* entryNumber) {
    char path[64];
    if (!buildPath(entryNumber, path, sizeof(path))) return false;
    if (!isMounted()) return false;
    if (!fileExists(path)) return true;  // already gone — not a failure
    return deleteFile(path);
}

}  // namespace storage
