#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sd_card.h"

namespace storage {

namespace {

constexpr const char* SETTINGS_PATH = "/settings.txt";
constexpr size_t MAX_FILE_SIZE = 2048;  // generous for ~6 short fields; catches a garbage/foreign file early

void trim(char* s) {
    // Leading whitespace.
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    // Trailing whitespace.
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

void setField(char* field, size_t fieldSize, const char* value) {
    strncpy(field, value, fieldSize - 1);
    field[fieldSize - 1] = '\0';
}

void applyLine(char* line, Settings* out) {
    trim(line);
    if (line[0] == '\0' || line[0] == '#') return;  // blank line or a comment — hand-editing needs both to be safe

    char* eq = strchr(line, '=');
    if (eq == nullptr) return;  // not a key=value line — ignore rather than fail the whole file over one typo
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;
    trim(key);
    trim(value);

    if (strcmp(key, "handheld_label") == 0) {
        setField(out->handheldLabel, sizeof(out->handheldLabel), value);
    } else if (strcmp(key, "judge_name") == 0) {
        setField(out->judgeName, sizeof(out->judgeName), value);
    } else if (strcmp(key, "wifi_ssid") == 0) {
        setField(out->wifiSsid, sizeof(out->wifiSsid), value);
    } else if (strcmp(key, "wifi_password") == 0) {
        setField(out->wifiPassword, sizeof(out->wifiPassword), value);
    } else if (strcmp(key, "home_base_address") == 0) {
        setField(out->homeBaseAddress, sizeof(out->homeBaseAddress), value);
    } else if (strcmp(key, "theme") == 0) {
        out->theme = (strcmp(value, "daylight") == 0) ? ThemeChoice::Daylight : ThemeChoice::Dark;
    } else if (strcmp(key, "sync_interval_seconds") == 0) {
        int parsed = atoi(value);
        if (parsed > 0) out->syncIntervalSeconds = parsed;  // 0/garbage keeps the struct default rather than a dead timer
    }
    // An unrecognized key is left alone, not an error — forward-compatible
    // with a settings file written by a newer/older firmware version.
}

}  // namespace

bool loadSettings(Settings* out) {
    *out = Settings{};  // every field starts at its documented default
    if (!isMounted()) return false;
    if (!fileExists(SETTINGS_PATH)) return true;  // no file yet is normal on a fresh card — defaults stand

    uint8_t buf[MAX_FILE_SIZE + 1];
    int n = readFile(SETTINGS_PATH, buf, MAX_FILE_SIZE);
    if (n <= 0) return true;  // unreadable is treated the same as absent — never blocks boot
    buf[n] = '\0';

    char* text = reinterpret_cast<char*>(buf);
    char* lineStart = text;
    for (char* p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            *p = '\0';
            applyLine(lineStart, out);
            lineStart = p + 1;
        }
    }
    applyLine(lineStart, out);  // last line, if the file doesn't end with a newline
    return true;
}

bool saveSettings(const Settings& settings) {
    char buf[MAX_FILE_SIZE];
    int len = snprintf(buf, sizeof(buf),
                        "# Top Spot Judging - handheld settings\n"
                        "# Edit with any plain text editor. Lines starting with # are ignored.\n"
                        "handheld_label=%s\n"
                        "judge_name=%s\n"
                        "wifi_ssid=%s\n"
                        "wifi_password=%s\n"
                        "home_base_address=%s\n"
                        "theme=%s\n"
                        "sync_interval_seconds=%d\n",
                        settings.handheldLabel, settings.judgeName, settings.wifiSsid, settings.wifiPassword,
                        settings.homeBaseAddress, settings.theme == ThemeChoice::Daylight ? "daylight" : "dark",
                        settings.syncIntervalSeconds);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(buf)) return false;
    return writeFileAtomic(SETTINGS_PATH, reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
}

}  // namespace storage
