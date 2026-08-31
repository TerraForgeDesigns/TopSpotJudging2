// Photo tracking for the microSD-primary / WiFi-fallback transfer flow —
// see ui/screens/photo_transfer_screen.h. Mirrors storage/pending_queue.h's
// shape: existence is never cached (a live SD-root scan is always the
// source of truth for WHICH photos exist, so it can never drift from
// reality), and only the one fact that genuinely needs persisting —
// whether a photo has been transferred to Home Base yet — is written to
// disk, in /photo_transfer_state.json.
//
// Filenames match photos_screen.cpp's buildPath() exactly:
// "/{entryNumber}_{car|sheet}.jpg" at the SD card root.
#pragma once

#include <cstdint>

namespace storage {

struct PhotoRecord {
    char entryNumber[8] = "";
    char type[8] = "";       // "car" | "sheet"
    char filename[24] = "";  // "{entryNumber}_{type}.jpg" — no leading slash
    uint32_t sizeBytes = 0;
    bool transferred = false;
};

// Scans the SD root for every *_car.jpg / *_sheet.jpg file (up to
// `maxOut`), cross-referenced against the persisted transferred set.
// Returns the count actually written.
int listPhotos(PhotoRecord* out, int maxOut);

int countPhotos();

// How many photos on the card have NOT been marked transferred yet —
// drives "Sending photo N of M" (network/photo_upload.h) and Clear
// Photos' gate (allTransferred() below).
int countUntransferred();

// True only when the card holds at least one photo and every one of them
// is marked transferred — Clear Photos refuses otherwise (see
// clearAllPhotos()). A card with ZERO photos is deliberately NOT
// "all transferred" for this gate's purposes; see clearAllPhotos()'s own
// doc for why that distinction matters.
bool allTransferred();

// Records `filename` as transferred — atomic write to
// /photo_transfer_state.json. Called once per photo, immediately after
// Home Base acknowledges it (POST /api/v1/photos/upload returns 200) —
// never batched, so an interrupted WiFi transfer resumes at the first
// still-untransferred photo rather than restarting. See
// network/photo_upload.h.
void markTransferred(const char* filename);

// Deletes every *_car.jpg / *_sheet.jpg at the SD root plus the transfer-
// state manifest. Refuses (returns false, deletes nothing) unless
// countPhotos() > 0 AND allTransferred() — the task's explicit "refuse
// unless the photos are marked transferred." This is the ONLY function
// in this firmware that ever deletes a photo; nothing else may call
// SD.remove() on a *_car.jpg/*_sheet.jpg path. The caller (Clear Photos)
// is responsible for getting an explicit, strongly-worded confirmation
// first — this function itself performs no confirmation of its own.
bool clearAllPhotos();

}  // namespace storage
