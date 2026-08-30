# DECISIONS.md — Architectural Decision Log

Running log of decisions made and questions still open. Update this whenever a real
architectural choice gets made or reversed — this is the file future sessions check to
avoid re-litigating settled questions, and to know what's still unresolved before
building on top of it. Newest entries at the top of each section.

## Decided

- **Photos never travel by USB — corrected throughout the codebase and docs, not
  just the UI text.** The handheld's USB-C port is a CH340C serial programming
  bridge, and the ESP32-S3's native USB pins are already committed to the
  touchscreen (see firmware/include/pins.h) — it can never present itself as a
  drive to Home Base's computer. The only direct transfer path is the operator
  physically removing the handheld's microSD card and inserting it into this
  computer's card reader; Wi-Fi upload is the sole fallback. This was wrong
  everywhere "USB" appeared — CONTEXT.md's workflow section, PROTOCOL.md's
  photo-transfer prose, README.md's event-day steps, and the app itself:
  - `TransferMethod.USB` → `TransferMethod.SD_CARD`. No migration needed —
    verified by running `alembic revision --autogenerate` against the model
    change and confirming it produced an empty upgrade/downgrade: this column
    uses `native_enum=False` with no `create_constraint`, so it's a plain
    string column with no DB-level constraint naming the old value.
  - `services/usb_watcher.py` → `services/sd_card_watcher.py`
    (`UsbWatcherThread` → `SdCardWatcherThread`); `services/usb_windows.py` →
    `services/sd_card_windows.py`. The underlying Win32 mechanism is
    unchanged and genuinely OS-level generic (`DRIVE_REMOVABLE` doesn't care
    whether the media is an SD card or a USB flash drive) — only the naming,
    which was actively misleading about what transport this system uses, was
    wrong. `app/main.py` and `web/photos.py` updated to match.
  - User-facing text now says plainly what to do: "Take the memory card out
    of the handheld and put it in this computer," not "Plug the handheld
    into USB (or insert its SD card)," which offered a nonexistent option.
  - **The "done" import summary was restructured to lead with the plain
    sentence CONTEXT.md's workflow implies: "N photos imported. M need
    attention."** — consolidating unmatched + duplicate + error counts into
    one "needs attention" figure, with the per-category pills kept below for
    anyone who wants the breakdown. "Skipped" (an already-imported re-scan)
    is relabeled "already imported," since a no-op re-scan needs no one's
    attention — see photo_ingest.py's `_find_already_imported`.
- **Per-car photo status added to the Cars table** (`services/cars.py::CarRow`
  gained `has_car_photo`/`has_judge_sheet_photo`) — one batched query across
  the whole roster's photos, not one query per row, since this table can hold
  400+ cars. A `DUPLICATE` photo still counts as "present" for this column's
  purpose (findability before the ceremony — something usable exists); the
  separate question of which copy is canonical is /photos' job, tracked
  independently. Rendered as a pill: "Both," "1 of 2," or — only once a car
  is actually judged — "Missing" in the conflict/red color, since an unjudged
  car having no photos yet isn't noteworthy but a judged one is.
- **`tests/test_photo_ingest.py` written from scratch — no coverage existed
  for the shared ingest path before this task**, despite it being exactly
  the function both transports call into. Covers filename parsing
  (case-insensitivity, `.jpeg`, embedded underscores), matched/duplicate/
  unmatched outcomes, copy-never-move (the source file is asserted to still
  exist after every ingest), thumbnail generation, idempotent re-scan, and
  unmatched resolution (including its own idempotency). Uses `monkeypatch`
  to point `photo_ingest.PHOTOS_DIR` at a `tmp_path` per test rather than
  writing into the real photo store.
- **POST /api/v1/sync brought fully into line with PROTOCOL.md — field names
  and shapes kept exactly as documented there, not the paraphrased
  terminology ("JudgingResult," "client_closed_at_uptime_ms,"
  "submitted_range_max," "last_changed_revision") used to describe the work.**
  The task that drove this was explicit that PROTOCOL.md is what the firmware
  is built against and must be implemented exactly — so where a description
  of the work used different words for a concept PROTOCOL.md already names
  (`JudgingSubmission`, `closed_at_uptime_ms`, the submission's own
  `score_range_max`, `Car.data_revision_at_change`), the document's actual
  name was kept rather than renaming the codebase to match an informal
  paraphrase. Real changes made:
  - **Score validation**: an active category with no score, or a score
    outside `1..score_range_max`, now rejects the WHOLE item (`status:
    "error"`, naming the category and the problem) before anything is
    written. This didn't exist before — a missing category score was
    previously silently absent from the stored result, and any integer was
    accepted regardless of range. Both are real correctness gaps: CONTEXT.md
    is explicit that there is no zero and no "not applicable" for an active
    category, and `scoring.py::build_ranking_key` already raises rather than
    defaulting a missing score to 0 (see the HB2 review's fix) — this closes
    the gap on the way IN, so that code path can't be reached by a bad
    submission in the first place. PROTOCOL.md documents this behavior now.
  - **Entry-detail corrections**: a submission's participant/year/make/
    model/vehicle_type now overwrites an existing non-empty value on the
    entry when the submission's value is non-empty AND different — not just
    fills blanks, as before. This is a judge correcting a wrong pre-fill, not
    a conflict; it's applied directly and logged (Python `logging`, not a
    stored audit row — nothing in the schema needed a place to put one, and
    LANGUAGE.md already treats this class of detail as belonging in
    diagnostic logs, not the interface). PROTOCOL.md documents the rule.
  - **`Handheld.last_config_revision`/`last_data_revision`** — new columns,
    updated on every sync from what the handheld itself reported at the top
    of the request. Purely informational (future host-facing "this handheld
    is behind" diagnostics); Home Base's own delta logic never reads them —
    it always compares against the request payload directly.
  - **New `services/vehicle_candidates.py`**: a manually-entered make or
    model now actually records a `VehicleCandidate`/`VehicleCandidateSighting`
    pair (find-by-normalized-key, increment `times_seen`, add a sighting) —
    this data existed as schema since HB1 but nothing ever wrote to it.
    `vehicle_additions` in the sync response is still always `[]`: deciding
    what counts as "approved" and computing a since-last-update delta for it
    is HB5's review-queue work, not this task's. PROTOCOL.md's
    `vehicle_additions (deferred)` section now describes exactly this split
    (sighting-recording live, approval/delta still pending) instead of
    implying nothing happens yet.
  - **Flagged, not resolved: PROTOCOL.md's documented `configuration.
    judge_chosen_awards` shape is `{id, name}` only.** The task describing
    this work also asked for awards in `configuration` to carry
    `chosen_by_judge`/`ranking_basis`. Neither appears in PROTOCOL.md's own
    example, and neither is something a handheld has any functional use for
    (nomination is "mark this car," not a ranking computation — that's
    entirely Home Base's job) — every award already in `judge_chosen_awards`
    is by construction judge-chosen, and ranking basis doesn't affect
    anything the handheld renders. Left unchanged (`{id, name}`) rather than
    guessed at, since PROTOCOL.md is the declared source of truth here;
    flagged for the user to confirm one way or the other rather than picked
    silently.
- **Cars, Add Cars, Judging Setup, and Awards management built for real — see
  CONTEXT.md's Cars and Edit Show sections.** Notable choices:
  - **Judging and Awards no longer have their own top-level dashboard pages —
    both now live as sections inside Edit Show.** The instruction that drove
    this ("EDIT SHOW, grouped by what the organiser is trying to do") lists
    Judging Setup and Awards as two of Edit Show's four groups, which
    conflicts with CONTEXT.md's original 8-tab dashboard IA (where Judging
    and Awards were separate top-level sections). Resolved by keeping the
    dashboard tab strip's 8 labels exactly as CONTEXT.md specifies, but
    pointing the "Judging" and "Awards" tabs at `/shows/{id}/edit#judging-
    setup` and `#awards-setup` — real anchors on the real Edit Show page,
    not separate pages. Visiting either highlights "Edit Show" as the active
    tab (no scroll-position tracking); that's genuinely which page you're on.
    `/criteria` and `/awards` as standalone routes are gone — nothing links
    to them anymore, see `web/stubs.py`.
  - **Add Cars renders its confirmation directly on the same page instead of
    redirecting.** A redirect-after-POST would have to carry "Added 25 cars,
    entries 311–335, and the range escalated to 1–10" through a URL — either
    truncating the message or leaking wire-style state into a query string.
    Rendering the Edit Show page directly from the POST handler (200, not
    303) keeps the full plain-language confirmation intact at the cost of
    the page not being a bookmarkable/refreshable URL after the action,
    which doesn't matter here.
  - **`services/cars.py::add_cars()` composes car-creation, the
    `show_data_revision` bump, and `check_and_escalate()` into one
    transaction, committing once at the end.** This is the exact scenario
    the transaction-boundary fix (see the HB2 review entry, below) was
    written for — see `test_add_cars_crossing_a_tier_escalates_and_converts_
    existing_scores` in `tests/test_cars.py`.
  - **`can_deactivate_category()`'s message now names an exact judged-car
    count** ("3 cars have already been judged on Paint..."), not just
    "cars have" — CONTEXT.md's protection-rule wording asked for this
    explicitly. Counts DISTINCT cars with an ACCEPTED submission scoring
    that category, not raw `JudgingScore` rows (which could double-count a
    car with a rejected/duplicate submission too).
  - **New services, each owning exactly one concern:** `services/cars.py`
    (Entry management — list/edit/add, never touches `entry_number` once
    set), `services/judging_categories.py` (post-creation category
    toggle/rename/reorder + Overall Impression, enforcing
    `judging_category_rules.py`'s guard), `services/awards_setup.py`
    (post-creation award add/rename/reorder/toggle/winner-choice — named
    `_setup`, not `awards.py`, to leave that name free for HB7's
    winner-*resolution* logic, which this module deliberately never
    touches). Every mutation in all three goes through
    `services/revisions.py` — verified by grep: `show.configuration_revision
    +=`/`show.show_data_revision +=` appear ONLY in revisions.py itself
    (two lines, one each). `car.data_revision_at_change = ...` appears in
    `services/cars.py` and `services/sync.py`, both only AFTER calling
    `bump_show_data_revision()` and both stamping with its already-bumped
    return value — that's the field a Car row uses to record which
    revision it last changed under, not a second place the counter itself
    gets written.
- **HB2 review fixes (six, applied together — see each for detail):**
  1. **`check_and_escalate()` no longer commits — the caller does, matching
     `services/revisions.py`'s existing contract, which it had been silently
     violating.** The bug this caused: nothing currently calls
     `check_and_escalate()` in production, but the moment something does (Add
     Cars: create entries, bump `show_data_revision`, escalate, as one
     all-or-nothing operation), escalation's own internal commit would lock in
     the car-adds and the revision bump regardless of whether a later step in
     that same logical operation failed — a rollback after that point does
     nothing, because there's nothing left uncommitted to roll back. Fixed by
     deleting the `db.commit()` call; `tests/test_range_escalation.py`'s
     callers now commit explicitly, and a new test
     (`test_failure_after_escalation_leaves_no_partial_state`) simulates
     exactly this failure mode and asserts nothing survives it — no cars, no
     revision bump, no escalation.
  2. **Turning a Judging Category ON is now blocked once any car in the show
     has been judged, symmetric to the existing OFF-with-scores block.**
     New `services/judging_category_rules.py` (`can_activate_category`,
     `can_deactivate_category`) — pure guard functions, no caller yet since
     post-creation category management isn't built (see the `/criteria`
     stub), written now so the rule is correct and tested before that screen
     exists rather than bolted on after. The activation check keys off
     whether the SHOW has judged cars, not whether the category itself has
     scores — a category that was off has no scores of its own, but that's
     exactly the problem, not evidence it's safe to turn on. Also:
     `scoring.py::build_ranking_key` no longer defaults a missing active-
     category score to 0 — it raises `MissingCategoryScoreError`. A 0-default
     was silently ranking a car as if it scored the worst possible in a
     category it was never asked about, indistinguishable in the output from
     a car that was actually judged and scored badly; CONTEXT.md is explicit
     that there's no such thing as a 0 or "not applicable" for an active
     category, so pretending one exists is worse than raising. Both
     directions tested in `tests/test_judging_category_rules.py`.
  3. **`ConfigurationOut.max_score` added, computed server-side as active
     categories × `score_range_max`.** Before this, a handheld had
     `score_range_max` and the category list but would have had to compute
     Max Score itself — two independent implementations of the same formula
     (CONTEXT.md's) that could drift out of sync with no way to detect it.
     PROTOCOL.md's `configuration` section gained a literal JSON example
     (it previously only described the shape in prose).
  4. **`JudgingCategory.sort_order` doing double duty as both display order
     and tie-break priority order (decided silently during the original
     ranking-kernel build) is now written down as a decision, not left
     implicit.** Trade-off, stated explicitly for the first time: an
     organiser cannot have a category display third to judges but break ties
     first — reordering one always reorders the other, because they're the
     same stored value. Accepted as a reasonable simplification — now stated
     directly in `models/category.py`'s docstring, not just here — but a
     real trade-off, and organisers dragging the
     Judging Setup reorder control for one reason must not be surprised that
     it silently changed the other. The wizard's Step 3 now says so directly:
     "This order also sets what judges see on the handheld — dragging to
     change one changes both."
  5. **`Show.status` (`setup` / `judging` / `finished`) added now, while the
     HB1 baseline migration is still the only structural one that mattered —
     see the schema-status note below.** Column default is `setup`;
     `services/show_wizard.py::materialize()` sets it straight to `judging`
     the moment a real Show exists, since the wizard itself IS the setup
     phase — there is no separate post-wizard "setup" state for a show that
     was just created. The `judging` → `finished` transition (HB7's
     finish-show gate) is explicitly NOT built here — only the column and
     the setup→judging move.
  6. **The vehicle-database spec is SPEC-B, in the master build guide — not
     "SPEC-C."** Every reference to a "not-yet-written SPEC-C"
     (`api/schemas.py`, `models/vehicle.py`, PROTOCOL.md, and this file's own
     Open section) was simply wrong about which document defines
     `vehicle_additions` — SPEC-B already exists and fully specifies the
     three-layer vehicle database, the review queue, and this field.
     Corrected everywhere; see Open, below, for what's still actually
     unbuilt (the HB5 services, not the spec).
- **Early-scaling nudge on the Number of Cars wizard step exists because
  score conversion is lossy in a specific, non-obvious way: it coarsens
  resolution, it doesn't add it.** A car judged at 1-5 and later scaled to
  1-25 can only ever land on 5, 10, 15, 20, or 25 — the conversion is exact
  arithmetic (`original × new_max / original_max`), but the judge's original
  choice only ever had 5 discrete values to begin with, so the scaled-up
  result inherits that coarseness permanently; it can never fill in the
  10 intermediate values a car judged fresh at 1-25 could land on. A host
  whose estimate is close to a tier threshold (140-150, 290-300) is
  gambling that their count won't tip over mid-show and coarsen every
  early car's resolution relative to later ones judged fresh at the
  higher range. Nudging them to enter their real expected total up front
  costs nothing and avoids that outcome entirely. See
  `services/show_wizard.py::near_threshold_nudge` and CONTEXT.md's Step 2.
- **Wizard state lives in one `ShowDraft` row's JSON blob, not normalized
  draft tables.** Considered mirroring the real schema with DraftCategory/
  DraftAward tables; rejected because a draft has no life beyond one
  browser session — nothing ever queries, joins, or migrates against it,
  and `materialize()` throws the whole row away the moment a real Show
  exists. A blob that's read whole and reassigned whole (never mutated
  in place — plain SQLAlchemy JSON columns don't track in-place dict
  mutation, so every service function in `show_wizard.py` builds a new
  dict and reassigns `draft.data`) is simpler and costs nothing extra
  here. See `models/show_draft.py`.
- **Wizard Steps 3 and 4 save every change immediately via small htmx
  POSTs against the draft; Steps 1, 2, and the Step 3/4 "Continue"
  actions are traditional submit-and-redirect.** The two "live" steps
  manage variable-length lists (which categories are on, tie-break
  order, which awards exist) where Max Score and the tie-break list both
  need to reflect a toggle the instant it happens — deferring that to a
  bulk save on Continue would mean either duplicating the recompute
  logic client-side in JS or showing stale numbers until submit. Steps 1
  and 2 are simple fixed fields with no live-recompute need, so a plain
  form post is the more boring, more debuggable choice — matches
  CONTEXT.md's resilience principle: no cleverness beyond what a given
  screen actually needs.
- **"Jump back and return" from Review uses a `from=review` query
  parameter threaded through step links and continue actions, not
  session state or a stored "furthest step."** Editing a section from
  Review saves that step's data exactly like normal, then redirects to
  Review instead of the next step in sequence — `from` never touches the
  ShowDraft or carries business data, it's pure navigation context, so
  it doesn't conflict with "keep wizard state server-side against a
  draft, not in hidden form fields." Implemented as a query parameter
  rather than a `Form(...)` field on the POST handlers — an earlier pass
  wired it as a hidden form field and it silently did nothing, since a
  value only present in the URL never reaches a `Form()`-declared
  parameter; caught by `test_edit_from_review_returns_to_review`.
- **`Award.active` was missing from the HB1 model rewrite — added now,
  not deferred.** CONTEXT.md's Awards section says Show Awards are
  "independently on/off," same as Judging Categories, but the HB1 award
  model only got `judge_chosen`/`ranking_basis`/`winner_car_id` — an
  oversight caught while building the wizard's Step 4, which needs
  exactly this to represent an award the host turned off without
  deleting it. Added via a normal incremental Alembic migration
  (`674d2e0674cd`) on top of HB1's single clean baseline — going
  forward, schema changes are ordinary migrations, not new baselines;
  HB1's "one clean initial migration" was specific to that
  reconciliation task, not a standing policy. That migration also makes
  `Show.location` optional and adds `Show.notes`, both needed for the
  wizard's Step 1 (Location was always required before; Notes didn't
  exist). Note it needed hand-adjustment after `--autogenerate`: SQLite
  has no native `ALTER COLUMN`, so the location-nullable change required
  wrapping in `op.batch_alter_table`, which autogenerate doesn't add on
  its own.
- **Show creation is now the wizard's job exclusively —
  `services/shows.py::create_show` was removed, not kept as a
  fallback.** The old plain 3-field "New show" form is gone from
  `shows/list.html`, replaced with a link to `/shows/new`. Keeping both
  would mean two divergent ways to end up with a Show row — one with
  entries/categories/awards, one without — for no benefit; nothing
  needs the old path once the wizard exists. `services/shows.py` keeps
  list/switch/edit, which the wizard doesn't do.
- **The Show Dashboard's 8 named sections (CONTEXT.md: Overview, Cars,
  Judging, Handhelds, Awards, Photos, Results, Edit Show) are a tab
  strip (`components/macros.html::dashboard_tabs`), and the sidebar was
  cut down to just Dashboard/Shows/Styleguide.** The old flat sidebar
  (Cars, Judging Categories, Photos, Conflicts, Results, Awards) doesn't
  match "exactly these sections" — Conflicts in particular isn't one of
  the 8, so it dropped out of primary navigation entirely; the route
  still exists (`/conflicts`, still a placeholder) and is reachable
  directly, it's just not linked from anywhere until it has a real home.
  Handhelds is a new real page (`/handhelds`, `web/handhelds.py`) — the
  same data Overview's panel already shows, on its own for when the host
  wants only that. Cars/Judging/Awards/Results keep their existing
  placeholder content from HB1 (now under the tab strip, with updated
  messages reflecting that Judging Setup happens in the wizard now) —
  full post-creation editing of categories/awards was not asked for in
  this task and was not built.
- **The Overview dashboard's "Awards Needing a Winner" stat only counts
  organiser-chosen (non-judge-chosen) awards with no `winner_car_id`
  set.** A judge-chosen award's real readiness depends on nomination and
  ranking logic (HB7) this build doesn't have — reporting a number for
  those would be either a lie (always 0) or would require building HB7
  early under a different name. Restricting the stat to what's
  genuinely computable now (an organiser-chosen award is either decided
  or it isn't) keeps it honest. "Cars Missing Photos" is fully real: a
  judged car counts if it lacks a MATCHED photo of either required type.
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
- **`vehicle_additions` / approved vehicle names — the spec is SPEC-B, in the master
  build guide, not "SPEC-C."** An earlier session referred to this as a "not-yet-written
  SPEC-C" — that was wrong, not just a placeholder name: SPEC-B already exists and
  fully defines the three-layer vehicle database, the review queue, and
  `vehicle_additions`. Corrected everywhere it was misnamed (`api/schemas.py`,
  `models/vehicle.py`, PROTOCOL.md) as part of the HB2 review fixes — see Decided,
  below. What's still open is only implementation: the tables exist
  (`app/models/vehicle.py`), but the services that populate, prepare, and review them
  are HB5's job, not built yet.
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
