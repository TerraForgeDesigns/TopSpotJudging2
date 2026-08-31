// Send Photos over Wi-Fi — the fallback path when the primary path
// (pulling the microSD card) isn't practical mid-show. See
// ui/screens/photo_transfer_screen.h and PROTOCOL.md's
// `POST /api/v1/photos/upload`.
//
// Same architecture as network/wifi_sync.h and the same reasoning for
// it, even though this is a judge-watched foreground action (unlike
// wifi_sync's unprompted periodic trigger): network::sync's OWN periodic
// timer keeps running regardless of which screen is showing, so a naive
// design where this module's background task touched storage/SD directly
// would race that timer's own storage reads/writes. So the same split
// applies — the background task does ONLY the HTTP POST for one
// already-built request buffer; the main thread does every storage read
// (loading a photo's bytes, building the multipart body) and write
// (storage::markTransferred()) between photos, one photo's task at a
// time, chained by a short-period LVGL timer.
//
// Resumable, never restarts: storage::markTransferred() is called the
// instant each individual photo's upload is acknowledged (200), not
// batched to the end — see storage/photo_state.h.
#pragma once

namespace network::photo_upload {

// Starts sending every currently-untransferred photo on the card, one at
// a time. No-op if a transfer is already in progress. Safe to call even
// with zero untransferred photos (isRunning() will simply never become
// true).
void startTransfer();

bool isRunning();

// 1-based "sending photo N of M" — both 0 when nothing is running or
// nothing was ever queued to send.
int currentIndex();
int total();

}  // namespace network::photo_upload
