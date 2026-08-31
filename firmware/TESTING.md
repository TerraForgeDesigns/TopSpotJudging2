# Handheld firmware — physical testing

This covers the two things that can't be verified by `pio run` alone: real
hardware behavior, and crash safety under an actual power loss. See each
bring-up file's own header comment for what to check on that specific
target; this file is the shared procedure they point back to.

## The battery-pull procedure

Why this exists: CONTEXT.md's resilience principle is "a judge losing an
hour of work is the worst failure this system has," and the only way to
know a design like `storage/pending_queue.h`'s (own file, written once,
`writeFileAtomic`) actually holds up is to interrupt it for real — not to
trust the comment describing it. A soft reset or `Ctrl+C` on the serial
monitor doesn't count: it lets in-flight SD writes finish. Only pulling the
battery (or power) mid-operation reproduces the failure mode this design
targets.

**Setup**: flash `bringup-storage`, open the serial monitor, keep the
microSD card in a card reader on hand so you can inspect it on a computer
between runs without reflashing.

1. **Baseline run.** Let `bringup-storage` run to completion once. Confirm
   every line in the serial log says `OK`, and `queue integrity` reports
   `0 unreadable`. Pull the card and confirm on a computer: `/settings.txt`
   is plain readable text; `/show.json`, `/entries.json`,
   `/sync_state.json`, `/queue/*.json`, `/drafts/*.json` all exist and are
   valid JSON.
2. **Reboot-persistence check.** Without reformatting the card, power-cycle
   the board and let it run again. `settings round trip` and
   `queue integrity` must reflect what the *previous* run wrote, not a
   fresh state — this proves persistence survives a reboot, not just a
   single power-on session.
3. **Mid-write pull, the queue (crash-critical file).** Reflash
   `bringup-storage` with the `enqueueCar` call itself as the interruption
   target: place the battery-pull at the moment between
   `writeFileAtomic()` opening `{path}.tmp` and the rename into
   `{path}.json` (a print statement immediately before `enqueueCar()` in
   the bring-up code, timed against the serial log, is precise enough to
   hit this window by hand). After the pull, remount the card on a
   computer and confirm:
   - `/queue/` contains **no partial `.json`** file under the real
     filename — either the previous run's complete file is there
     unchanged, or nothing is.
   - At most one leftover `{path}.tmp` — never a corrupt `{path}.json`.
   - Re-power the board and run `bringup-storage` again:
     `checkQueueIntegrity()` must report the `.tmp` as
     `orphanedTmpFiles` (and it gets deleted), `unreadable == 0`, and every
     previously-completed car still counted in `validCars`.
4. **Mid-write pull, a draft (in-progress car).** Same idea against
   `saveDraft()` — pull power while a draft write is in flight. Worst
   acceptable outcome: the draft reverts to its last successfully-saved
   state (the judge re-does at most the one screen's worth of unsaved
   input) or an orphaned `.tmp` under `/drafts/`. Never a truncated,
   half-written `{entry}.json` that `loadDraft()` chokes on.
5. **The full judging flow, on `bringup-judging`.** Flash
   `bringup-judging` and physically walk a car through Home -> Enter Car ->
   Vehicle Details -> Judge Car -> Award Nominations -> Photos -> Review ->
   Confirm (see that file's own header for the full checklist — entry
   resume, the duplicate-entry-number refusal, Confirm refusing without
   both photos). Pull the battery mid-flow at least once per screen across
   repeated runs:
   - **Any screen before Review**: on re-power, typing the same entry
     number on Enter Car must resume the draft at the furthest screen it
     reached — never restart from Vehicle Details, never come back blank.
   - **Mid-photo-capture**: the draft's `carPhotoSaved` /
     `sheetPhotoSaved` flags must only be true if the JPEG is actually
     complete on disk; a pull during the capture itself must leave the
     flag false so Review still blocks Confirm, not a truncated photo
     silently accepted as "saved."
   - **Mid-`finish()` (Review's Confirm)**: on re-power, the car is either
     fully queued (present under `/queue/`, draft gone, and Home's typed
     entry number now says "already judged on this device") or the draft
     is still sitting at Review, unqueued, and Confirm can be pressed
     again. Never both, never neither.

Record any deviation from the above against this file in DECISIONS.md —
don't just fix it silently, since the next session needs to know the
design was actually wrong somewhere, not just patched.

## Vehicle make/model lookup (F4)

**Setup**: `pio run -t upload-vehicle-seed` (needs a board attached — see
`tools/upload_vehicle_seed.py`) writes `firmware/data/vehicle_seed.bin` to
the `vehicle_seed` partition, separately from the normal firmware upload.
Regenerate that file first if `firmware/tools/vehicle_seed_source.json`
changed: `python firmware/tools/merge_seed_source.py` then
`python firmware/tools/build_vehicle_seed.py`.

1. **Search feels instant.** On Vehicle Details, tap Make, type a common
   prefix ("che") and a mid-word fragment that only matches a substring
   (something that appears mid-name, not at the start). Both must filter
   the list with no perceptible lag as each key is tapped — this is
   flagged, like every prior UI task's touch-latency/refresh checks, as
   something this environment cannot verify without a board; confirm on
   real hardware, not just the simulator.
2. **Recently Used, this show.** Pick a make/model, back out to Vehicle
   Details, re-enter the Make selector — the just-picked make should be
   first under Recently Used with no typing. Finish the show (or clear
   `/vehicle_recents.json` from the card) and confirm a NEW show starts
   with an empty Recently Used, not the old show's list.
3. **Other / Enter Manually, no vehicle_seed partition flashed.** On a
   board `upload-vehicle-seed` was never run on (or after erasing just
   that partition), open the Make selector: the list should show only
   "Other / Enter Manually" — never a crash, never a blank/frozen screen.
   Confirm manual entry still completes normally end to end.
4. **Other / Enter Manually, no SD card.** Pull the SD card, open the
   Make selector: Recently Used should simply be absent (no error), the
   seed-backed search/list should still work (it doesn't touch the SD
   card at all — see `storage::vehicle_db`), and Other/manual entry still
   completes normally.
5. **Search box carries typed text into Other.** Type something that
   matches nothing (e.g. a nonsense string), tap Other / Enter Manually —
   the manual-entry keyboard must open with that exact text already in
   the field, per the task's "never type the same thing twice."
6. **Make -> Model navigation.** With Model blank, picking a Make must
   land directly on that Make's Model selector, no detour back through
   Vehicle Details. With a Model already set, picking a DIFFERENT Make
   whose model list doesn't include the current Model must clear it and
   also land on the Model selector; picking a Make whose list DOES still
   include the current Model must return straight to Vehicle Details with
   Model untouched.
7. **Year.** Type a 4-digit year inside the show's plausible range (1885
   through the show's own event year + 1) — the keypad should close on
   the 4th digit with no Done tap needed. Type a 4-digit year outside that
   range — it must stay open with Done disabled and the "Not a plausible
   year" hint shown, not silently accept it.

## WiFi sync (F5)

**Setup**: a running Home Base instance on the show LAN, `Settings.wifiSsid`/
`wifiPassword`/`homeBaseAddress` pointed at it, at least one finished car queued.
None of this can be exercised in this environment — no board, no live AP, no Home
Base instance reachable from here. Everything below is unverified until run on real
hardware; the code is traceable against PROTOCOL.md and the task spec, which is as
far as this environment can confirm it.

1. **Out of range, then back in range.** Keep the handheld's configured SSID
   unreachable — confirm the status bar stays on whatever it last showed (no
   flicker to "Updating" on a scan-only miss), and that `Settings screen` > `Update
   Check Interval`'s value visibly governs how often a periodic attempt fires (watch
   serial log timestamps, or add a temporary log line). Bring the AP into range —
   confirm the very next periodic tick or an "Update Now" tap picks it up
   immediately, no waiting for a doubled interval to expire.
2. **Backoff actually doubles and caps.** With the AP OFF the whole time, confirm
   successive periodic misses roughly double the wait (starting from Settings'
   configured base) up to 900s (15 min), never beyond.
3. **A finished car never waits on backoff.** Set the interval artificially high
   (15 min), then finish judging a car — confirm the sync attempt fires immediately
   (trigger (a)), not after the full interval.
4. **Home Base down, AP up.** Run the AP but stop Home Base (or block port 8000) —
   confirm the queue is completely untouched afterward (car count on Home screen
   unchanged) and a toast reading "Home Base Not Connected" appears.
5. **Queue actually drains on success.** With Home Base up, confirm every currently
   queued car disappears from Home screen's "N cars waiting to send" line after a
   successful sync, and appears as a real judged car in Home Base's own Cars list.
6. **Re-submitting a car Home Base already has** (e.g. force a retry by killing the
   handheld's WiFi mid-attempt on a PREVIOUS run before this one) must come back
   `already_recorded` and still clear the queue entry — never treated as a
   duplicate conflict.
7. **Two handhelds, same car.** Judge the same entry number to completion on two
   handhelds — the second one to sync should see it removed from ITS queue with a
   small toast, and Home Base's Cars list should show that car flagged for the host
   to resolve, never silently overwritten.
8. **Scoring Updated notice.** While a car is queued and the judge is mid-way
   through judging a DIFFERENT car, escalate the show's range on Home Base (add
   enough cars, or however Home Base's own UI triggers it) and let a sync land —
   confirm the full-screen notice appears with the correct new range number, the
   judge is still on the same in-progress car underneath after tapping Continue, and
   the Judge Car screen's buttons show the new range immediately with no restart.
9. **Show Setup Updated toast.** Change something in Show Setup that ISN'T the
   range (rename a category, add an award) — confirm a plain "Show Setup Updated"
   toast, not the full-screen notice.
10. **20-minute warning.** Prevent any successful sync for over 20 minutes (AP off,
    or Home Base down) with at least one queued car — confirm the Home screen shows
    the prominent "Your scores are saved on this device..." banner, and that it
    goes away again once a sync actually succeeds.

## Photo transfer and diagnostics (F6)

None of this can be exercised in this environment — no board, no camera, no live
Home Base to upload photos to. Everything below is unverified until run on real
hardware; the code is traceable against the task spec, which is as far as this
environment can confirm.

1. **Safe removal actually leaves nothing open.** Judge a car with both photos
   taken, go to Settings > Manage Photos, tap Transfer Photos — confirm "Safe to
   remove the memory card." appears, then physically pull the card. Reinsert it,
   tap Card Reinserted, confirm every screen that touches storage works normally
   again (Home screen's progress number, Enter Car, Settings) — nothing should
   still think the card is missing.
2. **WiFi photo transfer resumes, not restarts.** With several untransferred
   photos and a live Home Base, tap Send Photos over Wi-Fi, watch "Sending photo
   N of M" advance, then kill WiFi (or Home Base) partway through. Confirm the
   already-sent photos stay marked transferred (re-open Manage Photos — their
   count should reflect it), then restore the connection and tap Send Photos
   over Wi-Fi again — confirm it picks up at the FIRST still-untransferred
   photo, never re-sending ones already acknowledged.
3. **Clear Photos actually refuses.** With at least one untransferred photo,
   confirm the Clear Photos button is disabled. Transfer everything (either
   path), confirm it becomes enabled, confirm the modal's strong wording and
   that Cancel leaves every photo untouched. Confirm on the real device: after
   confirming, the photos are actually gone from the card (check with a card
   reader) and the manifest file is gone too.
4. **Camera-absent judging flow.** With NO camera physically attached (or a
   deliberately disconnected one), confirm: firmware boots normally, display/
   touch/SD/WiFi/vehicle lookup all work, the full judging flow up through
   Award Nominations works normally, Photos shows the "Camera Problem" banner
   with both capture buttons disabled (not just erroring after a tap), and
   nothing anywhere crashes or reboots. Confirm Diagnostics (see below) shows
   "Unavailable" and a real last-error message, not blank fields.
5. **Diagnostics reveal gesture.** Confirm there is NO visible button, icon, or
   affordance hinting the hint label on Settings is interactive — then confirm a
   genuine long-press (not a quick tap) opens Diagnostics, and a quick tap does
   NOT. Confirm every listed fact reads something sensible (not garbage/
   uninitialized values) with and without WiFi connected, with and without the
   vehicle seed partition flashed, with and without an SD card present.
6. **720p capture timing, now with a real transfer cost attached.** DECISIONS.md
   already flags `CAPTURE_MODE_PHOTO`'s file size/timing as unverified (F3) —
   this task adds a new reason it matters: confirm a real 720p JPEG's WiFi
   upload time is tolerable for a judge actually waiting on "Sending photo N of
   M" for a realistic batch size, not just that the mechanism works for one
   photo.

## Non-crash-safety checks

Each bring-up target's own header comment (`bringup_display.cpp`,
`bringup_sd.cpp`, `bringup_camera.cpp`, `bringup_ui.cpp`,
`bringup_storage.cpp`, `bringup_judging.cpp`) lists what to physically
verify beyond crash safety — display refresh quality, touch latency feel,
camera preview correctness, theme persistence, and (for `bringup-judging`)
the full happy-path judging flow and the duplicate/missing-photo refusals.
Read the specific file before flashing it.
