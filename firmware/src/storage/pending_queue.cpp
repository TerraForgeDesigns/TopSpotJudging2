#include "pending_queue.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

#include "sd_card.h"

namespace storage {

namespace {

constexpr const char* QUEUE_DIR = "/queue";
constexpr size_t MAX_CAR_FILE_SIZE = 4096;  // one car's scores/details — generous, never close to this in practice

bool buildPath(const QueuedCar& car, char* out, size_t outSize) {
    int n = snprintf(out, outSize, "%s/%s_%lu.json", QUEUE_DIR, car.entryNumber,
                      static_cast<unsigned long>(car.closedAtUptimeMs));
    return n > 0 && static_cast<size_t>(n) < outSize;
}

size_t serializeCar(const QueuedCar& car, char* buf, size_t bufSize) {
    StaticJsonDocument<MAX_CAR_FILE_SIZE * 2> doc;
    doc["entry_number"] = car.entryNumber;
    doc["judge_name"] = car.judgeName;
    doc["closed_at_uptime_ms"] = car.closedAtUptimeMs;
    doc["participant"] = car.participant;
    doc["year"] = car.year;
    doc["make"] = car.make;
    doc["model"] = car.model;
    doc["vehicle_type"] = car.vehicleType;
    doc["make_manually_entered"] = car.makeManuallyEntered;
    doc["model_manually_entered"] = car.modelManuallyEntered;
    doc["score_range_max"] = car.scoreRangeMax;

    JsonArray scores = doc.createNestedArray("scores");
    for (int i = 0; i < car.scoreCount; i++) {
        JsonObject s = scores.createNestedObject();
        s["category_id"] = car.scores[i].categoryId;
        s["points"] = car.scores[i].points;
    }

    if (car.hasOverallImpression) {
        doc["overall_impression"] = car.overallImpression;
    } else {
        doc["overall_impression"] = nullptr;
    }

    JsonArray noms = doc.createNestedArray("nominations");
    for (int i = 0; i < car.nominationCount; i++) noms.add(car.nominations[i]);

    return serializeJson(doc, buf, bufSize);
}

}  // namespace

bool enqueueCar(const QueuedCar& car) {
    if (!ensureDir(QUEUE_DIR)) return false;

    char path[64];
    if (!buildPath(car, path, sizeof(path))) return false;

    char buf[MAX_CAR_FILE_SIZE];
    size_t len = serializeCar(car, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;

    // A file at this exact path should never already exist (the filename
    // embeds closed_at_uptime_ms, unique per close), but writeFileAtomic
    // handles that safely regardless — see its own header comment.
    return writeFileAtomic(path, reinterpret_cast<const uint8_t*>(buf), len);
}

int countQueued() {
    if (!isMounted()) return 0;
    File dir = SD.open(QUEUE_DIR);
    if (!dir || !dir.isDirectory()) return 0;

    int count = 0;
    File entry = dir.openNextFile();
    while (entry) {
        String name = entry.name();
        bool isTmp = name.endsWith(".tmp");
        bool isJson = name.endsWith(".json");
        entry.close();
        if (isJson && !isTmp) count++;
        entry = dir.openNextFile();
    }
    dir.close();
    return count;
}

bool isEntryQueued(const char* entryNumber) {
    if (!isMounted()) return false;
    File dir = SD.open(QUEUE_DIR);
    if (!dir || !dir.isDirectory()) return false;

    size_t prefixLen = strlen(entryNumber);
    bool found = false;
    File entry = dir.openNextFile();
    while (entry && !found) {
        String name = entry.name();
        entry.close();
        // Filenames are "{entry_number}_{closed_at_uptime_ms}.json" — a
        // match requires the entry number followed by '_', not just a
        // prefix match (so "042" doesn't false-match a real "0420...").
        if (name.length() > prefixLen && name.startsWith(entryNumber) && name.charAt(prefixLen) == '_') {
            found = true;
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return found;
}

QueueIntegrityReport checkQueueIntegrity() {
    QueueIntegrityReport report;
    if (!isMounted()) return report;
    File dir = SD.open(QUEUE_DIR);
    if (!dir || !dir.isDirectory()) return report;  // no queue directory yet = nothing queued, not an error

    File entry = dir.openNextFile();
    while (entry) {
        String name = entry.name();
        String fullPath = String(QUEUE_DIR) + "/" + name;
        bool isTmp = name.endsWith(".tmp");
        size_t size = entry.size();
        entry.close();
        report.totalFiles++;

        if (isTmp) {
            report.orphanedTmpFiles++;
            SD.remove(fullPath);  // never a real car — see this file's header comment
        } else if (name.endsWith(".json")) {
            uint8_t buf[MAX_CAR_FILE_SIZE];
            int n = readFile(fullPath.c_str(), buf, sizeof(buf) - 1);
            bool valid = false;
            if (n > 0 && static_cast<size_t>(n) == size) {
                buf[n] = '\0';
                StaticJsonDocument<MAX_CAR_FILE_SIZE * 2> doc;
                valid = !deserializeJson(doc, buf, n) && doc.containsKey("entry_number");
            }
            if (valid) {
                report.validCars++;
            } else {
                report.unreadable++;  // left in place untouched — see header comment, this is reported, never deleted
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return report;
}

}  // namespace storage
