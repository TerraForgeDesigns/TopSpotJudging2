// Device settings — handheld label, judge name, show Wi-Fi credentials,
// Home Base address, theme. Stored as a plain `key=value` text file
// (`/settings.txt`), one setting per line — deliberately NOT JSON, unlike
// every other file this firmware writes. This is the one file a
// non-technical person is expected to open in a text editor and hand-fix
// at a show (a wrong Home Base IP address typed on-device, a WiFi
// password typo) without reflashing or understanding JSON syntax. Every
// value this firmware itself writes back out is written in the same
// format, so a hand-edit and an on-device edit are always interchangeable.
#pragma once

#include <cstdint>

namespace storage {

enum class ThemeChoice { Dark, Daylight };

struct Settings {
    char handheldLabel[32] = "";
    char judgeName[64] = "";
    char wifiSsid[32] = "";
    char wifiPassword[64] = "";
    char homeBaseAddress[64] = "192.168.8.1:8000";  // the GL.iNet router's default LAN gateway
    ThemeChoice theme = ThemeChoice::Dark;
    // The BASE interval network::sync's periodic trigger checks at when
    // out of range — PROTOCOL.md: "default 3 minutes, configurable," and
    // the task's own instruction: "from settings, not hardcoded." Doubles
    // (capped at 900s/15min) on a miss and resets back to exactly this
    // value on a hit — see network/wifi_sync.h. Settings screen exposes
    // this in whole minutes (1-15); stored here in seconds to match
    // storage::SyncState::currentRetryIntervalSeconds' own unit.
    int syncIntervalSeconds = 180;
};

// Loads /settings.txt. Missing/unreadable/malformed lines are never a
// hard failure — each field simply keeps its default (see CONTEXT.md's
// resilience principle: a bad settings file must never block booting).
// Returns false only when the SD card itself isn't usable at all.
bool loadSettings(Settings* out);

// Atomic write (see sd_card.h) — a battery pull mid-save can never leave
// /settings.txt half-written.
bool saveSettings(const Settings& settings);

}  // namespace storage
