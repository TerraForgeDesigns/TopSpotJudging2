# DECISIONS.md — Architectural Decision Log

Running log of decisions made and questions still open. Update this whenever a real
architectural choice gets made or reversed — this is the file future sessions check to
avoid re-litigating settled questions, and to know what's still unresolved before
building on top of it. Newest entries at the top of each section.

## Decided

- **Major show-model overhaul (Aug 2026 spec update): no roster import, ever.**
  CSV/spreadsheet import is removed entirely — it will not be built. At show creation
  the organiser only states a car count; Home Base generates that many sequential
  entry numbers (001, 002, ... zero-padded to three digits) with every field empty.
  Whoever reaches an entry first — a judge in the field, or the host correcting it
  later — fills in Participant/Year/Make/Model. This replaces "Registration Number"
  (paper form, pre-existing) with **Entry Number** (system-generated) everywhere in
  the vocabulary — see CONTEXT.md's glossary. **Existing homebase code
  (`services/car_import.py`, the CSV import UI) is now spec-obsolete** — flagged as an
  OPEN item below for a dedicated future implementation prompt; this session changed
  specs only, no app code.
- **Score range is fully automatic, and only ever moves up.** Set from car count
  (1-150→1-5, 151-300→1-10, 301+→1-25, open-ended), never an organiser setting. Once a
  show crosses a threshold it never drops back, even if cars are later removed —
  removing a car must never recalculate other scores downward. Judging Categories are
  a fixed set of five built-ins (Engine, Exterior, Interior, Paint, Wheels/Tires), each
  independently on/off and renameable; **no mechanism to add new categories will be
  built.** Max Score = active categories × range max, always computed. Full rules and
  the score table live in CONTEXT.md.
- **Score conversion on range escalation uses decimal arithmetic with explicit
  ROUND_HALF_UP, never Python's built-in `round()`.** `round()`'s banker's rounding
  would turn `2.5` into `2` instead of `3`, silently changing a judge's original
  scoring intent. Formula and verified fixture tables are in CONTEXT.md. All three of
  `original_points`, `original_range_max`, and `adjusted_points` are stored — the
  original is never overwritten, and every later conversion re-derives from it rather
  than compounding rounding error across multiple escalations.
- **Tie-breaking is a primary mechanism, not an edge case — formalized as a 4-step
  cascade** (total adjusted score → Overall Impression if enabled → organiser-defined
  category priority order → ask the organiser, never silent, never by database id).
  Rationale: auto-scaling the score range keeps ranking *resolution* roughly constant
  rather than improving it, because car count grows with the range — the
  organiser-provided density figures for this are 7.1, 6.5, and 4.1 cars per distinct
  total across the three tiers, and at 400 cars on the 1–25 tier there are only 121
  possible totals with real judging clustering near the top. Ties at award boundaries
  are the normal case this system is built for, not a rare exception. **This resolves
  the old "Tie-break rule when two cars have identical total scores" OPEN item**
  (removed from Open, below).
- **Overall Impression is a Show Setup toggle, off by default, used only for
  tie-breaking.** One extra per-car score in the show's current range when enabled;
  never counts toward the total or Max Score. Off by default because at the 1-25 tier
  it costs judges an entire extra screen per car, and most shows won't need it.
- **Awards redesigned around two mechanisms: Top Awards (fully automatic, organiser
  only picks a count from a fixed list) and Show Awards (seven configurable built-ins,
  nomination-based for judge-chosen ones).** A judge-chosen Show Award is a
  *nomination* during judging, not a score or a pick — the judge is saying a car
  belongs in the conversation; Home Base alone determines the winner among nominated
  cars, ranked by that award's basis (own-category score for Best Paint/Interior/
  Engine, Total score for Best Car/Truck/Bike/Rat Rod), with its own tie cascade
  (basis → Overall Impression if enabled → Total score if not already the basis →
  organiser decides). An organiser-chosen award (created by answering "Will judges
  choose this winner?" with No) never reaches a handheld at all — Home Base's "Choose
  Winner" lets the organiser pick any car directly, which is the right shape for
  Sponsor's/Mayor's/People's Choice or a memorial award with no scoring rule.
  Switching an award from judge-chosen to organiser-chosen mid-show **keeps existing
  nominations in the database but stops using them for the winner**, and Home Base
  states this plainly rather than silently dropping them.
- **Car Class is removed, superseded by the nomination-based Show Awards system.** The
  new spec's awards model (Best Car/Truck/Bike/Rat Rod, judge-nominated, ranked by
  Total score) replaces what "Best in Class" was doing, and nothing in the Aug 2026
  spec references Car Class or class-based grouping. **This is an inference, not
  something the spec update stated outright — flagged to the user for confirmation**
  (see Open, below) before the `CarClass` model, its show-setup UI, and any
  class-based award logic are actually deleted from `/homebase`. Also resolves the old
  "Whether the show uses Car Classes at all" OPEN item (removed from Open, below).
- **Sync redesigned around two monotonically increasing revision counters
  (`configuration_revision`, `show_data_revision`) instead of timestamps, on a single
  unified `POST /api/v1/sync` endpoint** that replaces the old `GET /sync/roster` +
  `POST /sync/submissions` pair. Rationale: handhelds have no battery-backed clock and
  take their time from Home Base, so comparing integers across devices is more
  reliable than comparing timestamps that could be wrong before the first sync of the
  day. Idempotency key is now the triple `(entry_number, handheld_id,
  closed_at_uptime_ms)` — a retry with the same triple returns `"already_recorded"`
  and changes nothing, distinct from a genuine conflict (same entry_number, different
  `closed_at_uptime_ms`). **`score_range_max` is mandatory on every submission** so
  Home Base can convert a submission that was scored before a range escalation the
  handheld hasn't caught up to yet. Full contract in PROTOCOL.md. **Existing homebase
  code implementing the old two-endpoint protocol is now spec-obsolete** — same
  future-prompt flag as the import code above.
- **LANGUAGE.md created — as binding as DESIGN.md.** Principle: the interface is
  operated by people running a car show, not developers; technical vocabulary (API,
  sync, criterion, configuration, payload, etc.) lives in code/database/diagnostic
  logs only, never in normal user-facing text. Carries the full never-use →
  use-instead mapping table and the error-message standard (every user-facing error
  states what happened and what to do). DESIGN.md now references it.
- **Handheld hardware contract locked in (F1 — hardware bring-up):** Elecrow CrowPanel
  ESP32 Display 7" (DIS08070H V3.0), ESP32-S3-WROOM-1-N4R8, 800x480 RGB parallel LCD
  (not SPI TFT), GT911 capacitive touch over I2C, onboard microSD, Arducam Mega 5MP
  SPI on a second dedicated SPI bus using repurposed UART0 (GPIO43/44) and I2S BCLK
  (GPIO42) pins, plus GPIO38 for camera CS. Full pin table with sourcing lives in
  `firmware/include/pins.h` — every pin was verified against Elecrow's own wiki and
  two independent working example sketches from the community reference repo Elecrow
  points users to for this board, not guessed. This resolves the "exact touchscreen
  and Arducam part numbers" item that was blocking firmware work (see old entry
  below, now superseded).
- **The Arducam Mega library (v3.0.0) always uses the global Arduino `SPI` object —
  it cannot be pointed at a separate `SPIClass` instance.** Read directly from the
  vendor source (`ArducamSpi.cpp`): its HAL calls bare `SPI.begin()`/`SPI.transfer()`
  with no pin arguments. Since the SD card is wired to genuinely separate GPIOs, using
  the SD library's default `SD.begin(cs)` — which *also* defaults to that same global
  `SPI` object — would have silently coupled camera and SD in software regardless of
  physical wiring. Fix: the camera gets the global `SPI` object, pre-configured with
  its own pins before `Arducam_Mega::begin()` runs (ESP32's `SPIClass::begin()` is a
  verified no-op on a second call, confirmed by reading `arduino-esp32`'s own
  `SPI.cpp`); the SD card gets its own independent `SPIClass(HSPI)` instance, passed
  explicitly via `SD.begin(cs, sdSPI, ...)` (confirmed against `arduino-esp32`'s
  `SD.h` signature). See `firmware/src/camera/camera.cpp` and
  `firmware/src/storage/sd_card.cpp`.
- **Arducam Mega's internal `waitI2cIdle()` has no timeout — it's a bare `while(...) ;`
  busy-wait**, confirmed by reading `ArducamCamera.c`. A missing or unresponsive
  camera can hang `begin()` or a capture forever, not just fail — worse than a crash,
  since it would freeze the whole single-threaded firmware during setup. Fix: every
  call into the library runs on an isolated FreeRTOS task; the caller polls with its
  own timeout and forcibly `vTaskDelete()`s the task if it doesn't finish in time,
  reporting the camera unavailable rather than propagating the hang. See
  `firmware/src/camera/camera.h`'s header comment for the full rationale.
- **Arducam Mega pinned via its GitHub tag (`ArduCAM/Arducam_Mega#v3.0.0`), not the
  PlatformIO registry.** The registry has no listing under the official vendor's own
  name — only a third-party mirror (`dennis-ard/Arducam_Mega`) at an older version
  (2.0.9). Pinning directly against the vendor's tag is both more current and more
  trustworthy than depending on an unofficial mirror staying in sync.
- **Firmware bring-up code is verified by actually compiling it (`pio run`), not just
  reviewed.** PlatformIO isn't installed system-wide in this environment, so it was
  installed into the project's Python venv and all four environments
  (`bringup-display`, `bringup-sd`, `bringup-camera`, `handheld`) were built to a
  successful link — this caught four real compile errors (a missing `<cstddef>`, a
  missing `driver/i2c.h` include the reference example had that the port of it
  dropped, and an aggregate-initialization issue with default member initializers)
  before ever reaching real hardware. `.pio/` is gitignored but left populated
  locally, so the one-time toolchain/library download doesn't need to happen again.
- **Presentation mode is a standalone document (`awards/present.html`), not an app-shell
  page with the sidebar hidden by CSS.** Zero app chrome ever enters the DOM, so there's
  nothing to flash or hide — matches DESIGN.md's "no page flash" for a screen an
  audience is watching. It links `tokens.css` + `base.css` (fonts, reset) +
  `presentation.css` directly rather than extending `base.html`.
- **All slides are server-rendered up front, in one page load; navigation only toggles
  CSS classes.** `services/awards.py::build_presentation_sequence` resolves every
  award's winner, photos, and score breakdown before the page ever reaches the
  browser — there's no per-slide fetch, so "preload the next slide" is mostly free
  (every `<img>` for every slide is already in the DOM from first paint, so the
  browser fetches them all immediately regardless of which slide is active; the JS's
  own `preloadAround()` is a defensive, idempotent no-op on top of that, not the real
  mechanism). This is also what makes the crossfade a single `opacity` transition
  with no loading state to hide.
- **An award with no resolvable winner (nothing scored, or an unresolved tie) is
  excluded from the presentation sequence entirely**, not shown broken. The Awards
  page reports "`X of Y ready`" and disables Start Presentation at zero — the host
  fixes it there, not mid-ceremony.
- **Fullscreen requires a start screen with a real click.** The Fullscreen API only
  grants a request inside a user-gesture handler, and navigating to `/awards/present`
  from a link doesn't count as one on the new page — so the page opens on a
  branded "Begin Presentation" button, whose click both requests fullscreen and
  starts slide 1. This doubles as a natural "ready?" beat right before the ceremony
  segment starts.
- **Reveal-per-slide state persists for the rest of the session, but always starts
  hidden the first time a slide is shown.** Going back to an already-revealed award
  doesn't re-hide it — there's no reason to manufacture suspense for a car the host
  already announced. Implemented as a plain `is-revealed` class toggle over a curtain
  layer already sitting in the DOM (opacity crossfade only — no repositioning
  animation), so "hidden behind the title until revealed" needed no JS-driven layout
  changes.
- **Escape's meaning is layered: close the judge-sheet overlay if it's open, else exit
  fullscreen, else (or once fullscreen has already exited, including via the browser's
  own native Escape handling) navigate back to `/awards`.** The `fullscreenchange`
  listener catches both paths to the same "actually leave presentation mode" outcome.
- **All presentation sizing uses `clamp()` against `vw`/`vh`, no fixed pixel values
  and no resolution-specific breakpoints.** 1920x1080 and 1280x720 are both 16:9, so
  proportional scaling from the same markup covers both; verified with real Playwright
  screenshots at both resolutions (not just reasoning about the CSS) — see the
  awards-presentation-mode session. No headless browser is wired into the test suite;
  this was an ad hoc check, not an automated one.
- **Award reorder uses native HTML5 drag-and-drop (`draggable`, no library), persisted
  via a small `fetch()` POST to `/awards/reorder` on drop** — the task asked for drag
  specifically (elsewhere in the app, Classes/Criteria reordering uses up/down buttons
  instead; this is a deliberate exception, not a new default pattern).
- **Standard competition ("1224") ranking, with every tie explicitly flagged.**
  `services/scoring.py::rank_by_score` is the single function every ranking (overall,
  per-class, per-criterion, and award suggestions) computes rank from. Equal scores
  share a rank and the next distinct score jumps to `(position + 1)`, never silently
  broken by insertion/id order. The tie-break OPEN item below is still unresolved — a
  future rule plugs in as a secondary sort key *inside* this one function, so no
  caller or template needs to change when it lands.
- **Unscored cars are excluded from rankings entirely, not shown as 0-point entries.**
  `services/scoring.py::list_scored_cars` only returns cars with an ACCEPTED
  submission — a car nobody's judged yet has no meaningful score, and appearing as a
  last-place 0 would misrepresent it as judged-and-bad rather than not-yet-judged.
- **The unmatched-submissions view merged into `/conflicts`, replacing the standalone
  `/submissions/unmatched` page from H3.** The conflict-resolution task explicitly
  asked to surface that holding state on the same page as flagged-conflict cars, with
  the assign/discard actions that page never had — rather than keep two separate
  "things needing host attention" pages, `/conflicts` is now the one place for both.
  The dashboard's unmatched-submissions banner and the Flagged Conflicts stat card
  both link here now.
- **Host-entered corrected submissions are attributed to a synthetic "Home Base (host
  correction)" handheld**, created via the same `get_or_create_handheld` used for real
  wire-protocol handhelds (services/sync.py) — reused rather than making
  `JudgingSubmission.handheld_id` nullable for one edge case. `JudgingSubmission.note`
  (new column) carries the host's rationale; visible wherever submissions are shown.
- **Resolving a conflict (accept-one or enter-corrected) always rejects every other
  competing submission on that car**, never deletes them — they stay queryable for
  audit even though only one is ever ACCEPTED at a time.
- **Award winners are stored (`Award.winner_car_id`), not computed live.** The
  auto-suggestion re-runs the relevant ranking at display time and is shown until the
  host explicitly saves an override (or the suggestion itself, to "lock it in") — a
  tied rank-1 suggests nothing (`AwardSuggestion.ambiguous`) rather than silently
  picking one. This is deliberately a separate concept from the live rankings: once
  set, a winner doesn't quietly change if new scores come in later (e.g. a
  post-ceremony conflict resolution), which matters for an award already announced.
- **One shared `ingest_photo_file()` for both transports.** USB and WiFi both end up
  producing a source file that matches the `{registration_number}_car.jpg` /
  `_sheet.jpg` naming convention — the WiFi endpoint (`POST /api/v1/photos/upload`)
  constructs that filename from its `registration_number`/`photo_type` form fields and
  writes it to a temp file before handing off, specifically so filename parsing,
  duplicate detection, and unmatched-holding logic exist in exactly one place. See
  `services/photo_ingest.py`.
- **PROTOCOL.md's naming convention (`_car`/`_sheet`) is the vocabulary for the WiFi
  upload's `photo_type` form field too** (`"car"` or `"sheet"`), not our internal
  `PhotoType` enum's `"judge_sheet"`. PROTOCOL.md never states the endpoint's exact
  allowed values — the filename convention is the only place it spells out these two
  terms, so the wire form field mirrors that rather than an internal enum value the
  document never mentions.
- **A photo is copied, never moved, from its source — and never overwritten.** A
  second photo for a car+type slot that's already filled is kept alongside the first
  as `PhotoStatus.DUPLICATE`; a registration number matching no car is held as
  `PhotoStatus.UNMATCHED` under `photos/<show_id>/_unmatched/`. Both need host
  resolution but neither is ever silently dropped — see CONTEXT.md: photos are
  irreplaceable after the show ends.
- **Re-scanning an SD card that's already been imported is a no-op, not a fresh
  duplicate.** Before copying, `ingest_photo_file` checks whether a file of the same
  byte size already exists in the destination slot (or `_unmatched/`) and skips if so.
  Judges' cards get reinserted all day (dead battery, "let me check", etc.) — without
  this, every reinsertion would pile up spurious duplicate rows.
- **Unmatched-photo resolution moves the file (not a copy) within our own managed
  `photos/` tree**, and reuses the same never-overwrite duplicate check as a fresh
  ingest. The "copy, don't move" rule is about protecting the judge's original SD-card
  file, not about how home base reorganizes its own already-ingested copies.
  `resolve_unmatched_photo` is a no-op if called on a photo that's already resolved
  (found via manual testing: a double-submitted form was re-shuffling an
  already-placed file into a spurious duplicate of itself — see
  `test_resolving_an_already_resolved_photo_is_a_no_op`).
- **USB removable-drive detection is Windows-only, isolated in `services/usb_windows.py`,
  using `ctypes` against the Win32 API** (no pywin32/wmi/psutil — Pillow is the only new
  dependency). `services/usb_watcher.py` never touches a Windows API directly;
  supporting another OS later means writing a sibling module with the same
  `list_removable_drives() -> list[Path]` signature and branching in one function,
  not touching the polling loop or `photo_ingest.py`.
- **The USB watcher scans and ingests automatically on drive detection** — no "click
  import" step. Ingest is filename-filtered and idempotent-on-rescan, so plugging in
  an unrelated drive is harmless (zero matches) and replugging the same card is a
  no-op, which makes auto-scan safe and matches "the host needs to see it working,
  not wonder if it's frozen." A manual "Scan connected drives" button (`POST
  /photos/scan`) exists alongside it for a host who doesn't want to wait for the
  watcher's poll cycle or wants to retry without unplugging/replugging.
- **Unmatched registration numbers are held, not rejected.** A submission for a
  registration number that doesn't yet exist in the roster (e.g. a late-registered
  car judged before the handheld's roster pull caught up) is accepted and stored as
  `SubmissionStatus.UNMATCHED` (`car_id = NULL`, `registration_number` kept as the
  handheld actually sent it) rather than bounced back as an error. At the wire level
  (PROTOCOL.md's `POST /sync/submissions` response) it's still reported as
  `"accepted"` — the wire protocol only defines `accepted` / `flagged_duplicate` /
  `error`, and a handheld doesn't need to know that host-side reconciliation is
  pending, only that its data was received and won't be lost. Surfaced in the home
  base UI at `/submissions/unmatched`, linked from a dashboard banner whenever the
  count is nonzero. When a car with that registration number is later added (single
  add, CSV import, or an edit that changes the reg number to match), it's
  auto-reconciled: the earliest-received held submission is promoted to accepted, and
  any additional ones for the same number flag a conflict exactly like a normal
  duplicate would — see `services/sync.py::reconcile_unmatched_for_car`.
- **The wire protocol's `handheld_id` is a string, matched against `Handheld.label`.**
  PROTOCOL.md's example (`"handheld_id": "hh-2"`) is a firmware-assigned string, not
  home base's integer primary key. There's no handheld provisioning UI yet, so the
  first sync from an unseen `handheld_id` auto-creates a `Handheld` row keyed on that
  string as its `label` (now unique). Revisit if handhelds ever need to be
  pre-provisioned or renamed independently of their wire identity.
- **`CarClass` has no `updated_at`, so `classes` in a roster sync is always sent in
  full**, never delta-filtered by `since` — classes are few and rarely change once a
  show starts, so this is cheap. `cars` and `criteria` (which do track `updated_at`)
  are properly delta-filtered.
- **An unknown judging-criteria name rejects the whole submission item, not just that
  score.** A car's total only means something if every criterion was recorded — a
  partially-imported score set would be a worse failure mode than an obvious,
  visible one. The result item's `status` is `"error"` with a message naming the
  unrecognized criteria name(s); nothing is written for that item.
- **Criteria matching for scoring is case-insensitive against every criterion the
  show has ever had, not just currently-active ones.** Deactivating a criterion
  (see below) only removes it from future roster pulls — a score a judge already
  recorded against it before deactivation must still resolve when the handheld
  eventually syncs.
- **Python + FastAPI + SQLite + server-rendered UI for home base.** Offline-friendly,
  no build step at runtime, no bundler/npm toolchain to keep working without internet
  at the event.
- **PlatformIO + Arduino framework for ESP32-S3 firmware.**
- **Registration number is the human-facing primary key and the photo-matching key.**
  *(Superseded by the Aug 2026 spec update above — see "Entry Number" in
  [CONTEXT.md](CONTEXT.md)'s glossary. Left here as history, not current guidance.)*
  See [PROTOCOL.md](PROTOCOL.md) photo naming convention.
- **Photos transfer post-judging only, USB primary / WiFi fallback.** Never during
  active judging — see CONTEXT.md workflow section for why.
- **Scan-first sync with backoff.** Full trigger/backoff model in PROTOCOL.md.
- **PC clock is the time authority; handhelds sync clock from `server_time`.** No NTP,
  ever — see CONTEXT.md offline-first constraint.

## Open

- **Confirm Car Class removal.** DECISIONS.md above treats Car Class as superseded by
  the new nomination-based Show Awards, but the Aug 2026 spec update never said so
  explicitly — this is this session's inference. Confirm before deleting `CarClass`,
  its show-setup UI, or any class-based award logic from `/homebase`.
- **Homebase app code is now stale against the Aug 2026 spec update — needs a
  dedicated implementation prompt.** Specifically: `services/car_import.py` and the
  CSV import UI (roster import is removed entirely — see Decided); the
  `GET /sync/roster` + `POST /sync/submissions` handlers (replaced by unified
  `POST /api/v1/sync`); anything keying off `registration_number` (renamed
  `entry_number` everywhere); the old fixed/organiser-chosen score range in show setup
  (now automatic); and the existing awards model (single winner-per-award, no
  nomination, no Top Awards). This spec-rewrite session made no app code changes per
  its own instructions — all of the above still reflects the pre-Aug-2026 design until
  a future prompt rebuilds it.
- **`vehicle_additions` / approved vehicle names ("SPEC-C") is referenced but not yet
  specified.** The Aug 2026 spec update names `make_manually_entered` /
  `model_manually_entered` on a submission and a `vehicle_additions` field in the sync
  response, pointing at a controlled make/model vocabulary spec that hasn't been
  written yet. PROTOCOL.md documents the field's presence in the wire contract; its
  actual semantics (what makes an addition "approved," how it propagates back to
  handhelds) are undefined until that spec arrives.
- **Exact field-level shape of the `configuration` object in `POST /api/v1/sync`'s
  response is inferred, not dictated.** PROTOCOL.md lists what it must logically carry
  (active Judging Categories with names/priority order, current score range, Overall
  Impression enabled flag, judge-chosen Show Awards for nomination) based on what
  handhelds need to render judging correctly, but the Aug 2026 spec update didn't give
  a field-by-field schema. Nail this down in the implementation prompt that builds the
  new sync endpoint.
- **How an entry number physically reaches a car in the field is unspecified.** The
  old workflow had a pre-printed paper registration form; the new one only specifies
  that Home Base generates entry numbers 001..N at show creation. Whether that means
  printed entry cards, a check-in station, or something else isn't part of the Aug
  2026 spec update — CONTEXT.md's workflow section deliberately doesn't invent an
  answer.
- **No duplicate-photo resolution UI yet.** `PhotoStatus.DUPLICATE` photos are counted
  in the import summary and kept on disk, but there's no screen to view them side by
  side and pick a winner (unlike unmatched photos, which do have a resolution view at
  `/photos/unmatched`) — the task that added photo ingest only specified an unmatched
  resolution view. Revisit if duplicates turn out to be common enough at a real show
  to need more than "the host opens the folder and looks."
- **No handheld provisioning UI yet.** Handhelds currently self-register on first
  sync (see the `handheld_id`-as-`label` decision above) — there's no home base
  screen to pre-register, rename, or deprovision one. Fine for now with 3–4 known
  devices; revisit if that becomes error-prone (e.g. a typo'd `handheld_id` on the
  firmware side silently creating a phantom handheld).
- **Battery voltage monitoring — no ADC pin identified yet.** The CrowPanel 7" has a
  JST battery connector with a charge circuit, but no documented voltage-sense ADC
  anywhere (checked Elecrow's wiki, both community example repos, and an ESPHome PR
  for this board). Need: a free ADC-capable GPIO from whatever's left unassigned, and
  confirmation of a resistor divider (100kΩ + 100kΩ recommended: halves a 4.2V full
  charge to a safe 2.1V at the ADC pin). `firmware/src/power/battery.h` deliberately
  fails to compile (`#error`) until `PIN_BATTERY_ADC` is added to `pins.h` — see that
  file. Blocks: battery bring-up test, `src/power/` sleep management.
- **GPIO38 possibly double-claimed — needs a continuity check before soldering.** One
  secondary source (an ESPHome community PR for this board) lists GPIO38 as "Touch
  INT," which would conflict with its assignment as Camera CS. The verified *working*
  reference example initializes GT911 touch with `INT=-1` (unused, pure I2C polling)
  successfully, so firmware proceeds on the assumption GPIO38 is genuinely free — but
  this wasn't independently confirmed against a schematic. A 30-second multimeter
  continuity check between the GT911's INT pad and GPIO38 before soldering the camera
  on would close this out for good.
- **Once the Arducam is soldered onto GPIO43/44, this board loses its normal
  USB-serial programming path** (UART0 via the CrowPanel's onboard bridge chip, which
  is exactly what's being repurposed). Not a blocker — firmware bring-up and
  iteration happens over UART0 before the camera's permanent installation, matching
  the project's own "eventually solder directly" plan — but worth remembering before
  reflashing becomes much less convenient. No native-USB fallback: GPIO19/20 (the
  ESP32-S3's fixed native USB pins) are permanently wired to GT911 touch I2C on this
  board.

## Notes for future sessions

- When an OPEN item gets resolved, move it to Decided with a one-line rationale, and
  update any doc (CONTEXT/DESIGN/PROTOCOL) whose content assumed the old open
  question.
- If a new decision reverses a prior one, don't delete the old entry — strike it or
  note the reversal and why, so the history of *why* survives.
