# DECISIONS.md — Architectural Decision Log

Active open items, and a one-line, dated summary of every decision made — full
reasoning for each lives in [docs/decisions-archive.md](docs/decisions-archive.md),
linked from every entry. Dates below are MM-DD, all 2026. Update this whenever a
real decision gets made or reversed; see "Notes for future sessions" at the bottom
for exactly where new content goes. Decided entries are in chronological order,
oldest at the top — append new ones at the end, matching the archive's own order.

## Decided

- **Photos never travel by USB — corrected to SD card everywhere (code, docs, UI text).** 08-30 — [why](docs/decisions-archive.md#d1)
- **Per-car photo status added to the Cars table (Both / 1 of 2 / Missing).** 08-30 — [why](docs/decisions-archive.md#d2)
- **`tests/test_photo_ingest.py` written from scratch — no coverage existed for the shared ingest path.** 08-30 — [why](docs/decisions-archive.md#d3)
- **POST /api/v1/sync brought fully into line with PROTOCOL.md's exact field names/shapes.** 08-30 — [why](docs/decisions-archive.md#d4)
- **Cars, Add Cars, Judging Setup, Awards management built for real per CONTEXT.md's Edit Show.** 08-30 — [why](docs/decisions-archive.md#d5)
- **HB2 review: six surgical fixes** (transaction boundary, category guard, max_score, tiebreak_priority, Show.status, SPEC-B naming). 08-30 — [why](docs/decisions-archive.md#d6)
- **Early-scaling nudge on Number of Cars — score conversion coarsens resolution, doesn't add it.** 08-30 — [why](docs/decisions-archive.md#d7)
- **Wizard state lives in one `ShowDraft` row's JSON blob, not normalized draft tables.** 08-30 — [why](docs/decisions-archive.md#d8)
- **Wizard Steps 3/4 save live via htmx; Steps 1/2 are traditional submit-and-redirect.** 08-30 — [why](docs/decisions-archive.md#d9)
- **"Jump back and return" from Review uses a `from=review` query param, not session state.** 08-30 — [why](docs/decisions-archive.md#d10)
- **`Award.active` was missing from the HB1 model rewrite — added now, not deferred.** 08-30 — [why](docs/decisions-archive.md#d11)
- **Show creation is now the wizard's job exclusively — the old plain-form path removed.** 08-30 — [why](docs/decisions-archive.md#d12)
- **Show Dashboard's 8 CONTEXT.md sections are a tab strip; sidebar cut to Dashboard/Shows/Styleguide.** 08-30 — [why](docs/decisions-archive.md#d13)
- **"Awards Needing a Winner" stat counts only organiser-chosen awards.** 08-30 — [why](docs/decisions-archive.md#d14)
- **Major show-model overhaul (Aug 2026 spec): no roster import, ever — entries start empty.** 08-30 — [why](docs/decisions-archive.md#d15)
- **Score range is fully automatic from car count, and only ever moves up.** 08-30 — [why](docs/decisions-archive.md#d16)
- **Score conversion uses decimal ROUND_HALF_UP, never Python's banker's-rounding `round()`.** 08-30 — [why](docs/decisions-archive.md#d17)
- **Tie-breaking formalized as a 4-step cascade — ties are the normal case, not an edge case.** 08-30 — [why](docs/decisions-archive.md#d18)
- **Overall Impression is a Show Setup toggle, off by default, used only for tie-breaking.** 08-30 — [why](docs/decisions-archive.md#d19)
- **Awards redesigned: automatic Top Awards + nomination-based judge-chosen Show Awards.** 08-30 — [why](docs/decisions-archive.md#d20)
- **Car Class removed, superseded by the nomination-based Show Awards system.** 08-30 — [why](docs/decisions-archive.md#d21)
- **Sync redesigned around two monotonic revision counters on one unified sync endpoint.** 08-30 — [why](docs/decisions-archive.md#d22)
- **LANGUAGE.md created — as binding as DESIGN.md.** 08-30 — [why](docs/decisions-archive.md#d23)
- **Handheld hardware contract locked in (F1) — CrowPanel 7"/ESP32-S3/GT911/Arducam Mega pin map.** 08-30 — [why](docs/decisions-archive.md#d24)
- **Arducam Mega library always uses the global `SPI` object — camera/SD kept on separate SPI instances.** 08-30 — [why](docs/decisions-archive.md#d25)
- **Arducam Mega's `waitI2cIdle()` has no timeout — every camera call runs on an abandon-able task.** 08-30 — [why](docs/decisions-archive.md#d26)
- **Arducam Mega pinned via its GitHub tag, not the stale third-party PlatformIO registry mirror.** 08-30 — [why](docs/decisions-archive.md#d27)
- **Firmware bring-up verified by actually compiling (`pio run`) — caught 4 real errors.** 08-30 — [why](docs/decisions-archive.md#d28)
- **Presentation mode is a standalone document, not the app shell with the sidebar CSS-hidden.** 08-30 — [why](docs/decisions-archive.md#d29)
- **All presentation slides are server-rendered up front; navigation only toggles CSS classes.** 08-30 — [why](docs/decisions-archive.md#d30)
- **An award with no resolvable winner is excluded from the presentation sequence, not shown broken.** 08-30 — [why](docs/decisions-archive.md#d31)
- **Fullscreen requires a real click — the Fullscreen API needs a user gesture on the new page.** 08-30 — [why](docs/decisions-archive.md#d32)
- **Reveal-per-slide state persists for the session; a re-visited slide doesn't re-hide.** 08-30 — [why](docs/decisions-archive.md#d33)
- **Escape's meaning is layered: close overlay, then exit fullscreen, then leave presentation mode.** 08-30 — [why](docs/decisions-archive.md#d34)
- **All presentation sizing uses `clamp()` against `vw`/`vh` — no fixed pixels, no breakpoints.** 08-30 — [why](docs/decisions-archive.md#d35)
- **Award reorder uses native HTML5 drag-and-drop, persisted via a small `fetch()` POST.** 08-30 — [why](docs/decisions-archive.md#d36)
- **Standard competition ("1224") ranking, every tie explicitly flagged, never silent.** 08-30 — [why](docs/decisions-archive.md#d37)
- **Unscored cars excluded from rankings entirely, never shown as 0-point entries.** 08-30 — [why](docs/decisions-archive.md#d38)
- **Unmatched-submissions view merged into `/conflicts`, replacing the old standalone page.** 08-30 — [why](docs/decisions-archive.md#d39)
- **Host-entered corrections attributed to a synthetic "Home Base (host correction)" handheld.** 08-30 — [why](docs/decisions-archive.md#d40)
- **Resolving a conflict rejects, never deletes, every other competing submission.** 08-30 — [why](docs/decisions-archive.md#d41)
- **Award winners are stored, not computed live — a tied suggestion never auto-applies.** 08-30 — [why](docs/decisions-archive.md#d42)
- **One shared `ingest_photo_file()` for both the SD-card and WiFi-upload transports.** 08-30 — [why](docs/decisions-archive.md#d43)
- **PROTOCOL.md's `_car`/`_sheet` naming is the wire vocabulary for WiFi upload too.** 08-30 — [why](docs/decisions-archive.md#d44)
- **A photo is copied, never moved, from its source, and an existing one never overwritten.** 08-30 — [why](docs/decisions-archive.md#d45)
- **Re-scanning an already-imported card is a no-op, not a fresh duplicate.** 08-30 — [why](docs/decisions-archive.md#d46)
- **Unmatched-photo resolution moves (not copies) the file within our own managed tree.** 08-30 — [why](docs/decisions-archive.md#d47)
- **Removable-drive detection is Windows-only, isolated behind one seam function.** 08-30 — [why](docs/decisions-archive.md#d48)
- **The card watcher scans/ingests automatically on drive detection — no "click import" step.** 08-30 — [why](docs/decisions-archive.md#d49)
- **Unmatched registration numbers held for host reconciliation, never rejected.** *(superseded, see spec-overhaul above)* 08-30 — [why](docs/decisions-archive.md#d50)
- **The wire protocol's `handheld_id` is a string, matched against `Handheld.label`.** 08-30 — [why](docs/decisions-archive.md#d51)
- **`CarClass` (pre-removal) had no `updated_at` — always sent in full.** *(superseded, Car Class removed)* 08-30 — [why](docs/decisions-archive.md#d52)
- **An unknown criteria name rejects the whole submission item, not just that score.** 08-30 — [why](docs/decisions-archive.md#d53)
- **Criteria matching is case-insensitive against every criterion the show ever had.** 08-30 — [why](docs/decisions-archive.md#d54)
- **Python + FastAPI + SQLite + server-rendered UI for Home Base — offline-friendly, no build step.** 08-30 — [why](docs/decisions-archive.md#d55)
- **PlatformIO + Arduino framework for ESP32-S3 firmware.** 08-30 — [why](docs/decisions-archive.md#d56)
- **Registration Number as primary/photo-matching key.** *(superseded by Entry Number)* 08-30 — [why](docs/decisions-archive.md#d57)
- **Photos transfer post-judging only, SD card primary / WiFi fallback — never during active judging.** 08-30 — [why](docs/decisions-archive.md#d58)
- **Scan-first sync with backoff.** 08-30 — [why](docs/decisions-archive.md#d59)
- **PC clock is the sole time authority; handhelds sync clock from `server_time`. No NTP, ever.** 08-30 — [why](docs/decisions-archive.md#d60)
- **HB7 — scoring, tie-breaking, conflicts, results, awards, finish-show gate, built for real.** 08-30 — [why](docs/decisions-archive.md#d61)
- **Awards presentation mode reuses the pre-spec-rewrite `presentation.css`/`.js` almost unchanged.** 08-31 — [why](docs/decisions-archive.md#d62)
- **The presentation walk reuses `Award.sort_order` as the ceremony order.** 08-31 — [why](docs/decisions-archive.md#d63)
- **Only Show Award winners are walked in the presentation, not the 10-200-car Top Awards list.** 08-31 — [why](docs/decisions-archive.md#d64)
- **F2 — Handheld UI: LVGL over LovyanGFX, full component library, leak-safe screen manager.** 08-31 — [why](docs/decisions-archive.md#d65)
- **F3 — Judging flow: crash-safe local storage (settings, drafts, finished-car queue) + Home-through-Review screens.** 08-31 — [why](docs/decisions-archive.md#d66)
- **F4 — Vehicle make/model lookup: flash-mapped seed database, Recently Used, Make/Model selector screens.** 08-31 — [why](docs/decisions-archive.md#d67)
- **F5 — Handheld WiFi sync: scan-first, backoff-aware, never-lose-a-queued-car, against the real sync API.** 08-31 — [why](docs/decisions-archive.md#d68)
- **F6 — Photo transfer and diagnostics: safe SD removal, WiFi fallback, Clear Photos, camera-failure handling.** 08-31 — [why](docs/decisions-archive.md#d69)
- **F7 — Power management: backlight dim/off, battery warnings, WiFi-off verified. Deep sleep NOT built.** 08-31 — [why](docs/decisions-archive.md#d70)
- **SIM1 — Browser handheld simulator, served by Home Base, talking to the real sync API.** 08-31 — [why](docs/decisions-archive.md#d71)
- **INT1 — End-to-end tests: headless handheld simulator, 310-car/4-handheld simulated show, show-day runbook.** 08-31 — [why](docs/decisions-archive.md#d72)
- **INT2 — Router setup guide; found and fixed a real bug (default Home Base address pointed at the router).** 08-31 — [why](docs/decisions-archive.md#d73)
- **Camera pin correction (pre-solder): MOSI/MISO moved off UART0 (GPIO43/44) onto I2S pins (GPIO17/18) at U11.** 08-31 — [why](docs/decisions-archive.md#d74)
- ~~Camera on GPIO43/44 (UART0) accepted as permanent console loss once soldered — filed "not a blocker."~~ REVERSED 08-31, see the entry directly above. [why](docs/decisions-archive.md#o13)

## Open

- **PRE-SOLDER GATE — GPIO38 possibly double-claimed (Camera CS vs. Touch INT).** Continuity check, GT911 INT pad → GPIO38, before soldering. No continuity → free, solder as documented. Continuity found → swap `PIN_CAMERA_CS` to the staged ALTERNATE (GPIO44), a one-line edit. Full procedure: `firmware/TESTING.md`'s Pre-Solder Checklist. [why](docs/decisions-archive.md#o12)
- **Battery voltage monitoring — no ADC pin identified yet.** No documented voltage-sense ADC anywhere in Elecrow's materials or either reference repo. `power/battery.h` refuses to compile until `PIN_BATTERY_ADC` is added to `pins.h`. Blocks battery bring-up + sleep management. [why](docs/decisions-archive.md#o11)
- **No mains/USB-power-detect signal exists on this board (F6).** Same gap class as battery ADC — no VBUS-sense pin documented. Blocks a real "not plugged in" warning on the WiFi photo-transfer screen. [why](docs/decisions-archive.md#o3)
- **True deep sleep between cars deferred to a future "End of Day" state (F7)** — blocked on the GPIO38 check above (the one candidate wake pin). Backlight dim/off built instead for now. [why](docs/decisions-archive.md#o2)
- **Vendored web fonts are placeholders, not the real distinct weights.** Archivo 600/700/800 and IBM Plex Sans 400/500/600 are each byte-identical. Doesn't break offline-ness; needs the real font downloads before a real show. [why](docs/decisions-archive.md#o1)
- **`camera::CAPTURE_MODE_PHOTO` (1280x720) is unverified against real hardware** — only QVGA has ever been tested on a board. Confirm capture time/file size/SD write duration once hardware exists. [why](docs/decisions-archive.md#o4)
- **No duplicate-photo resolution UI yet** — `PhotoStatus.DUPLICATE` photos are kept but have no side-by-side pick-a-winner screen, unlike unmatched photos. [why](docs/decisions-archive.md#o9)
- **No handheld provisioning UI yet** — handhelds self-register on first sync; fine for 3–4 known devices, revisit if a typo'd `handheld_id` creates a phantom one. [why](docs/decisions-archive.md#o10)
- **How an entry number physically reaches a car in the field is unspecified** — printed cards, a check-in station, or something else; not part of the Aug 2026 spec. [why](docs/decisions-archive.md#o8)
- **Confirm Car Class removal** — treated here as superseded by Show Awards, but the spec update never said so explicitly; this is an inference, confirm before deleting it. [why](docs/decisions-archive.md#o5)
- **Homebase app code is stale against the Aug 2026 spec in several places** — CSV import, the old two-endpoint sync handlers, `registration_number`, the old fixed range, the old awards model. Needs a dedicated implementation prompt. [why](docs/decisions-archive.md#o6)
- **`vehicle_additions` implementation (HB5) is still unbuilt** — spec (SPEC-B) is settled, tables exist, but the populate/prepare/review services don't. [why](docs/decisions-archive.md#o7)

## Notes for future sessions

- When an OPEN item gets resolved, move it to Decided as a new one-line summary
  (append at the end, with today's date), and write the full rationale into
  docs/decisions-archive.md, not here — this file stays one line per decision.
- New rationale for anything — a fresh decision, a reversal, an Open item's
  backstory — is written into docs/decisions-archive.md, appended in order, with
  a matching one-line summary + anchor link added here. Never write long-form
  reasoning directly into this file again; that's exactly what made it 138 KB.
- If a new decision reverses a prior one, don't delete the old entry — strike it
  (`~~like this~~`) in whichever section it lives in, and note the reversal and
  why, so the history of *why* survives. See the struck line under Decided for
  the pattern (the UART0/camera pin reversal).
- Update any doc (CONTEXT/DESIGN/PROTOCOL) whose content assumed an old open
  question the moment it resolves.
