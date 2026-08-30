# DECISIONS.md — Architectural Decision Log

Running log of decisions made and questions still open. Update this whenever a real
architectural choice gets made or reversed — this is the file future sessions check to
avoid re-litigating settled questions, and to know what's still unresolved before
building on top of it. Newest entries at the top of each section.

## Decided

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
  See [CONTEXT.md](CONTEXT.md) glossary and [PROTOCOL.md](PROTOCOL.md) photo naming
  convention.
- **Photos transfer post-judging only, USB primary / WiFi fallback.** Never during
  active judging — see CONTEXT.md workflow section for why.
- **Scan-first sync with backoff.** Full trigger/backoff model in PROTOCOL.md.
- **PC clock is the time authority; handhelds sync clock from `server_time`.** No NTP,
  ever — see CONTEXT.md offline-first constraint.

## Open

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
- **Tie-break rule when two cars have identical total scores.** Currently surfaced to
  the host for a manual decision in the results/awards flow — no automatic rule.
  Revisit if hosts want this automated later.
- **Exact touchscreen and Arducam part numbers.** Needed before firmware
  display/camera work can start — display driver and camera init code are part-number
  specific. Blocks: firmware display layer, firmware camera capture layer.
- **Whether the show uses Car Classes at all, or judges one combined field.** Affects
  whether "Best in Class" awards are always available or need to be conditionally
  hidden/disabled when a show doesn't define classes. Affects home base show-setup UI
  and the awards presentation flow.

## Notes for future sessions

- When an OPEN item gets resolved, move it to Decided with a one-line rationale, and
  update any doc (CONTEXT/DESIGN/PROTOCOL) whose content assumed the old open
  question.
- If a new decision reverses a prior one, don't delete the old entry — strike it or
  note the reversal and why, so the history of *why* survives.
