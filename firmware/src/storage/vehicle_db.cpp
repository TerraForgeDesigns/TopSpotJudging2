#include "vehicle_db.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>
#include <strings.h>  // strncasecmp
#include <esp_partition.h>

namespace storage::vehicle_db {

namespace {

constexpr const char* PARTITION_NAME = "vehicle_seed";
constexpr esp_partition_subtype_t SEED_SUBTYPE = static_cast<esp_partition_subtype_t>(0x40);  // matches ../../partitions.csv

constexpr const char* LEARNED_NAMESPACE = "veh_learn";
constexpr const char* LEARNED_KEY = "models";  // NVS keys cap at 15 chars — everything learned lives under this one key, see header comment
constexpr size_t MAX_LEARNED_JSON_SIZE = 8192;

// Mirrors exactly what tools/build_vehicle_seed.py writes — see that
// file's own header for the authoritative format writeup. `packed` (not
// manual per-field memcpy) is what keeps multi-byte field reads safe
// against a flash-mapped, byte-addressable pointer: GCC on Xtensa emits
// byte-composing loads for a packed struct's fields automatically, so
// there's no unaligned-access fault risk either way.
struct __attribute__((packed)) Header {
    char magic[4];
    uint16_t version;
    uint16_t reserved0;
    uint32_t makeCount;
    uint32_t modelCount;
    uint32_t makeTableOffset;
    uint32_t modelTableOffset;
    uint32_t stringBlobOffset;
    uint32_t stringBlobSize;
};

struct __attribute__((packed)) MakeEntry {
    uint32_t nameOffset;
    uint32_t firstModelIndex;
    uint16_t modelCount;
    uint16_t reserved;
};

struct __attribute__((packed)) ModelEntry {
    uint32_t nameOffset;
};

const uint8_t* g_mapped = nullptr;
const Header* g_header = nullptr;
const MakeEntry* g_makes = nullptr;
const ModelEntry* g_models = nullptr;
spi_flash_mmap_handle_t g_mmapHandle;
bool g_seedOk = false;

// Reads the length-prefixed string at `stringOffset` (relative to the
// string blob, exactly as build_vehicle_seed.py wrote it) into `out`
// (MAX_NAME_LEN+1 bytes), null-terminated.
void readString(uint32_t stringOffset, char* out) {
    const uint8_t* p = g_mapped + g_header->stringBlobOffset + stringOffset;
    uint8_t len = p[0];
    if (len > MAX_NAME_LEN) len = MAX_NAME_LEN;  // defensive only — build_vehicle_seed.py already enforces this
    memcpy(out, p + 1, len);
    out[len] = '\0';
}

bool containsCaseInsensitive(const char* haystack, const char* needle) {
    if (needle[0] == '\0') return true;
    size_t needleLen = strlen(needle);
    for (const char* p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, needleLen) == 0) return true;
    }
    return false;
}

void setBounded(char* dst, const char* src) {
    strncpy(dst, src, MAX_NAME_LEN);
    dst[MAX_NAME_LEN] = '\0';
}

// Binary search for an EXACT (case-insensitive) make name — the one place
// in this module binary search is a genuine win over a linear scan. It
// works here because the make table is sorted by name and this is an
// exact lookup; findMakes()/findModels() below are deliberately full
// linear scans instead, because a substring match ("matches anywhere in
// the name") can land in the middle of an entry that sorted order gives
// no shortcut to — and with only a few hundred makes, the linear scan
// costs well under a millisecond on this MCU regardless. Returns the
// make's index, or -1.
int findExactMakeIndex(const char* make) {
    if (!g_seedOk) return -1;
    int lo = 0, hi = static_cast<int>(g_header->makeCount) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        char name[MAX_NAME_LEN + 1];
        readString(g_makes[mid].nameOffset, name);
        int cmp = strcasecmp(name, make);
        if (cmp == 0) return mid;
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

// --- Learned store ---

Preferences& prefs() {
    static Preferences p;
    return p;
}

// Loads the learned-store JSON blob (a flat [{make, model}, ...] array)
// into `doc`. Returns false — doc left empty — if the store has never
// been written or fails to parse: never a crash, just "no learned
// vehicles yet," the normal state until a future sync module starts
// calling saveLearnedVehicle() (see this module's header comment).
bool loadLearned(DynamicJsonDocument& doc) {
    Preferences& p = prefs();
    if (!p.begin(LEARNED_NAMESPACE, /*readOnly=*/true)) return false;
    size_t len = p.getBytesLength(LEARNED_KEY);
    bool ok = false;
    if (len > 0 && len <= MAX_LEARNED_JSON_SIZE) {
        static uint8_t buf[MAX_LEARNED_JSON_SIZE + 1];  // static: this module is single-threaded/reentrant-free, same convention as other storage/*.cpp fixed buffers
        p.getBytes(LEARNED_KEY, buf, len);
        buf[len] = '\0';
        ok = deserializeJson(doc, buf, len) == DeserializationError::Ok;
    }
    p.end();
    return ok;
}

}  // namespace

void init() {
    g_seedOk = false;
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SEED_SUBTYPE, PARTITION_NAME);
    if (part == nullptr) return;  // never flashed (pio run -t upload-vehicle-seed not yet run) — degrade, never crash

    const void* mapped = nullptr;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &mapped, &g_mmapHandle);
    if (err != ESP_OK || mapped == nullptr) return;

    g_mapped = static_cast<const uint8_t*>(mapped);
    g_header = reinterpret_cast<const Header*>(g_mapped);
    if (memcmp(g_header->magic, "VSD1", 4) != 0) {
        g_mapped = nullptr;
        g_header = nullptr;
        return;  // not a valid seed image (wrong build, corrupt flash) — degrade, don't crash
    }
    g_makes = reinterpret_cast<const MakeEntry*>(g_mapped + g_header->makeTableOffset);
    g_models = reinterpret_cast<const ModelEntry*>(g_mapped + g_header->modelTableOffset);
    g_seedOk = true;
}

bool seedAvailable() { return g_seedOk; }

bool getSeedInfo(uint16_t* version, uint32_t* makeCount, uint32_t* modelCount) {
    if (!g_seedOk) return false;
    *version = g_header->version;
    *makeCount = g_header->makeCount;
    *modelCount = g_header->modelCount;
    return true;
}

int findMakes(const char* query, MakeMatch* out, int maxOut) {
    int shown = 0;
    int matched = 0;

    if (g_seedOk) {
        for (uint32_t i = 0; i < g_header->makeCount; i++) {
            char name[MAX_NAME_LEN + 1];
            readString(g_makes[i].nameOffset, name);
            if (!containsCaseInsensitive(name, query)) continue;
            matched++;
            if (shown < maxOut) setBounded(out[shown++].name, name);
        }
    }

    DynamicJsonDocument doc(MAX_LEARNED_JSON_SIZE * 2);
    if (loadLearned(doc)) {
        for (JsonVariantConst e : doc.as<JsonArrayConst>()) {
            const char* make = e["make"] | "";
            if (make[0] == '\0' || !containsCaseInsensitive(make, query)) continue;
            if (findExactMakeIndex(make) >= 0) continue;  // already counted via seed
            bool dup = false;
            for (int i = 0; i < shown; i++) {
                if (strcasecmp(out[i].name, make) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            matched++;
            if (shown < maxOut) setBounded(out[shown++].name, make);
        }
    }
    return matched;
}

int findModels(const char* make, const char* query, ModelMatch* out, int maxOut) {
    int shown = 0;
    int matched = 0;

    int makeIdx = findExactMakeIndex(make);
    if (makeIdx >= 0) {
        const MakeEntry& me = g_makes[makeIdx];
        for (uint16_t i = 0; i < me.modelCount; i++) {
            char name[MAX_NAME_LEN + 1];
            readString(g_models[me.firstModelIndex + i].nameOffset, name);
            if (!containsCaseInsensitive(name, query)) continue;
            matched++;
            if (shown < maxOut) setBounded(out[shown++].name, name);
        }
    }

    DynamicJsonDocument doc(MAX_LEARNED_JSON_SIZE * 2);
    if (loadLearned(doc)) {
        for (JsonVariantConst e : doc.as<JsonArrayConst>()) {
            const char* eMake = e["make"] | "";
            const char* eModel = e["model"] | "";
            if (eMake[0] == '\0' || eModel[0] == '\0') continue;
            if (strcasecmp(eMake, make) != 0) continue;
            if (!containsCaseInsensitive(eModel, query)) continue;
            bool dup = false;
            for (int i = 0; i < shown; i++) {
                if (strcasecmp(out[i].name, eModel) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            matched++;
            if (shown < maxOut) setBounded(out[shown++].name, eModel);
        }
    }
    return matched;
}

bool modelBelongsToMake(const char* make, const char* model) {
    int makeIdx = findExactMakeIndex(make);
    if (makeIdx >= 0) {
        const MakeEntry& me = g_makes[makeIdx];
        for (uint16_t i = 0; i < me.modelCount; i++) {
            char name[MAX_NAME_LEN + 1];
            readString(g_models[me.firstModelIndex + i].nameOffset, name);
            if (strcasecmp(name, model) == 0) return true;
        }
    }

    DynamicJsonDocument doc(MAX_LEARNED_JSON_SIZE * 2);
    if (loadLearned(doc)) {
        for (JsonVariantConst e : doc.as<JsonArrayConst>()) {
            const char* eMake = e["make"] | "";
            const char* eModel = e["model"] | "";
            if (strcasecmp(eMake, make) == 0 && strcasecmp(eModel, model) == 0) return true;
        }
    }
    return false;
}

void saveLearnedVehicle(const char* make, const char* model) {
    if (make == nullptr || model == nullptr || make[0] == '\0' || model[0] == '\0') return;

    DynamicJsonDocument doc(MAX_LEARNED_JSON_SIZE * 2);
    bool hadData = loadLearned(doc);
    JsonArray arr = (hadData && doc.is<JsonArray>()) ? doc.as<JsonArray>() : doc.to<JsonArray>();

    for (JsonVariantConst e : arr) {
        if (strcasecmp(e["make"] | "", make) == 0 && strcasecmp(e["model"] | "", model) == 0) {
            return;  // already learned — nothing to do
        }
    }

    JsonObject obj = arr.createNestedObject();
    obj["make"] = make;
    obj["model"] = model;

    char buf[MAX_LEARNED_JSON_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    // Growing past the cap is treated as "drop this one" rather than a
    // write failure worth surfacing — at the expected scale (occasional
    // admin-approved additions, not free-form judge input; see this
    // module's header comment) this should never happen in practice.
    if (len == 0 || len >= sizeof(buf)) return;

    Preferences& p = prefs();
    if (!p.begin(LEARNED_NAMESPACE, /*readOnly=*/false)) return;
    p.putBytes(LEARNED_KEY, buf, len);
    p.end();
}

}  // namespace storage::vehicle_db
