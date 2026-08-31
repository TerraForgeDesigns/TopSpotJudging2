// The single in-progress car every judging screen (Vehicle Details ->
// Judge Car -> Award Nominations -> Photos -> Review) reads from and
// writes into. Backed directly by storage::DraftCar — there is no
// separate in-memory-only copy that could drift from what's on disk, so
// "save after every screen transition" (see DECISIONS.md's F3 resume
// decision) is just "write the same struct this session already is."
#pragma once

#include "storage/drafts.h"

namespace ui::judging {

// True if a car is currently being judged (Enter Car has resolved an
// entry number and this session is active).
bool isActive();

const char* entryNumber();

// Loads an existing draft for `entryNumber` if one exists (the resume
// case), or starts a brand-new one at storage::DraftStep::VehicleDetails.
// Either way, `current()` is valid after this call. `scoreRangeMax` is
// the show's CURRENT range (read at runtime — see judge_car_screen.*);
// only applied when starting fresh, since a resumed draft already has
// whatever range was in effect when it was started (CONTEXT.md: the
// range only ever moves up, never re-scored retroactively mid-car).
void start(const char* entryNumber, int scoreRangeMax);

storage::DraftCar& current();

// Persists `current()` to /drafts/{entryNumber}.json (atomic write) —
// call after every meaningful change, and always on leaving a screen in
// the judging flow. Returns false on a write failure; callers that can
// show it should (LANGUAGE.md style), but a single failed autosave is
// not itself fatal to the flow — the next one (or Review's own
// save-then-queue) gets another chance.
bool save();

// Moves the session into the finished-car queue (storage::enqueueCar) —
// only ever called from the Review screen's Confirm action, and only
// once both photos are verified saved (see DraftCar::carPhotoSaved /
// sheetPhotoSaved). Deletes the draft and clears the session on success.
// Returns false (session left untouched) if the queue write couldn't be
// verified — Review must not proceed as if the car were finished.
bool finish();

// Deletes the draft and clears the session without queuing anything —
// the judge's explicit "discard this car" path, distinct from just
// navigating Home (which leaves the draft in place to resume later; see
// DECISIONS.md).
void discard();

}  // namespace ui::judging
