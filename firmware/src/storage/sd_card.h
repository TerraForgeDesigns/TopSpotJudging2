// Onboard microSD — SPI, on its own dedicated hardware SPI peripheral.
//
// IMPORTANT: this uses a SEPARATE SPIClass instance (HSPI) from the
// Arducam Mega, which is hardcoded (in its own library source) to always
// use the global default `SPI` object (FSPI). Even though SD and camera
// are wired to genuinely separate GPIOs per the hardware contract, calling
// the standard SD.begin(cs) — which implicitly uses the same global `SPI`
// the camera needs — would silently couple them in software regardless of
// how they're wired. See DECISIONS.md and src/camera/camera.cpp for the
// matching half of this fix.
#pragma once

#include <cstddef>
#include <cstdint>

namespace storage {

struct SdInfo {
    bool mounted = false;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes() const { return mounted && totalBytes >= usedBytes ? totalBytes - usedBytes : 0; }
};

// Mounts the SD card on its dedicated SPI bus. Returns false (never
// hangs, never crashes) if no card is present or the mount fails — SD
// failure must be a visible, recoverable error, per CONTEXT.md's
// resilience principle, since local photo persistence is required before
// a judging record can be considered complete.
bool begin();

bool isMounted();

// Re-reads capacity/free-space; call after begin() and after any write
// you want reflected immediately (e.g. for the bring-up test's report).
SdInfo getInfo();

// Writes `data` (length `len`) to `path` (e.g. "/test.txt"), overwriting
// if it exists. Returns false on any failure (not mounted, write error,
// short write). NOT crash-safe on its own — a battery pull mid-write
// leaves `path` truncated/corrupt. Use writeFileAtomic() for anything
// that must survive that (queue entries, show cache, settings, sync
// state) — see CONTEXT.md's resilience principle.
bool writeFile(const char* path, const uint8_t* data, size_t len);

// Crash-safe write: writes to `path` + ".tmp", flushes, closes, verifies
// the temp file's size matches `len` by reopening and checking, deletes
// any existing file at `path`, then renames the temp file into place.
// `path` is only ever observed in its old (complete) state or its new
// (complete) state — a battery pull at any point during this leaves
// `path` itself untouched (worst case: an orphaned .tmp file, which the
// caller's own boot-time scan can ignore or clean up; it was never
// renamed, so it was never "the real file").
//
// The one narrow gap: if `path` already existed, there's a brief window
// between deleting it and renaming the temp file in where NEITHER exists
// — a crash in exactly that window loses the OLD value. This is
// deliberately accepted only for files that are cheap to reconstruct
// (settings, the show cache, sync state — all re-enterable or
// re-fetchable) — see storage/pending_queue.h, which sidesteps this
// entirely by never overwriting an existing file at all (each finished
// car gets its own uniquely-named file, written once).
bool writeFileAtomic(const char* path, const uint8_t* data, size_t len);

// Directories don't nest-create automatically under SD.mkdir() on this
// library — creates every path segment in turn. Safe to call when the
// directory already exists (checks first).
bool ensureDir(const char* path);

// Reads up to `maxLen` bytes from `path` into `outBuf`. Returns the
// number of bytes actually read, or -1 if the file doesn't exist / isn't
// readable.
int readFile(const char* path, uint8_t* outBuf, size_t maxLen);

bool fileExists(const char* path);
bool deleteFile(const char* path);

}  // namespace storage
