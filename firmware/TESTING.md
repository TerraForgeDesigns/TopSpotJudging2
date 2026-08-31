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

## Non-crash-safety checks

Each bring-up target's own header comment (`bringup_display.cpp`,
`bringup_sd.cpp`, `bringup_camera.cpp`, `bringup_ui.cpp`,
`bringup_storage.cpp`, `bringup_judging.cpp`) lists what to physically
verify beyond crash safety — display refresh quality, touch latency feel,
camera preview correctness, theme persistence, and (for `bringup-judging`)
the full happy-path judging flow and the duplicate/missing-photo refusals.
Read the specific file before flashing it.
