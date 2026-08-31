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
- **HB7 — Scoring, tie-breaking, conflicts, results, awards, finish show (this
  task):**
  1. **`JudgingCategory.built_in_key` added** — a stable identity string ("engine",
     "exterior", "interior", "paint", "wheels_tires") set once at show creation by
     `services/show_wizard.py::materialize()`, never touched by a rename. Discovered
     while building `award_results.py`: `Award.ranking_basis` needs to reliably find
     "the Engine category" even after an organiser renames it to "Motor," and matching
     by `name` would silently break the moment that happens. `name` is what's shown;
     `built_in_key` is what code matches on. Nullable, so pre-existing dev data
     degrades to "not usable as a ranking basis" instead of failing to load.
  2. **Top Awards boundary-tie resolution is stored as a set of car ids
     (`Show.top_awards_resolved_car_ids`), not a recomputed placement list.**
     `services/results.py::compute_top_awards()` is a pure function over an already-
     ranked list — it always distinguishes "the tied group exactly fills the
     remaining slots" (no real choice, resolved automatically) from "more cars are
     tied than there's room for" (CONTEXT.md's "4 cars are tied for the last 2
     places" case), and only the second case ever needs storage. The ranking itself
     is never cached — recomputed fresh every time, same as everywhere else in this
     codebase.
  3. **`Award.marked_not_presented` is a separate boolean from "no winner set yet,"**
     mutually exclusive with `winner_car_id` (setting one clears the other via
     `award_results.py::choose_winner`/`mark_not_presented`). Needed because
     `services/finish_show.py`'s gate must be able to tell "this award still needs a
     decision" apart from "this award was decided not to happen this year" —
     collapsing them into one nullable `winner_car_id` would make a genuinely
     unresolved award indistinguishable from a deliberately skipped one.
  4. **Award winners are suggested live, never auto-applied.** Reapplies the same
     principle DECISIONS.md already recorded for the pre-Aug-2026 awards model:
     `award_results.py::suggest_award_winner()` computes the highest-ranked nominee
     on every call and returns `ambiguous=True` with no entry if rank 1 is itself a
     tie; only an explicit host action (`use_suggested_winner()` or `choose_winner()`)
     ever writes `Award.winner_car_id`. A tied suggestion is never silently broken by
     database id or insertion order.
  5. **`services/finish_show.py`'s gate treats a judge-chosen award with zero
     nominations as outstanding, worded differently from "has nominees but no
     confirmed winner."** CONTEXT.md calls this out by name — a Show Award nobody
     nominated anyone for is easy to forget entirely, not just easy to leave
     undecided. An inactive award (`Award.active is False`) is never outstanding; it
     was never going to be presented.
  6. **Finishing a show does not bump either revision counter.** Unlike every other
     Show Setup or car-data change, `ShowStatus.JUDGING -> FINISHED` doesn't change
     anything a handheld's sync response needs to reflect — see
     `services/revisions.py`'s docstring for what each counter is actually for.
  7. **The Awards dashboard tab now points at `/awards` (winner resolution: nominee
     lists, Choose Winner, mark-not-presented, Finish Show), not Edit Show's award
     SLOT setup (`#awards-setup` — add/rename/reorder/toggle/change who picks).**
     HB3 originally routed the tab into Edit Show because winner resolution didn't
     exist yet; now that it does, setup and resolution are different enough tasks
     (configuring award slots vs. deciding who won) to warrant separate pages. Slot
     setup stays reachable via a link from the Awards page.
  8. **The printable Results view (`/results/print`) is a standalone HTML document
     that doesn't extend `base.html` and inlines its own CSS**, rather than reusing
     the app shell — CONTEXT.md asks for a print view that "works offline with sane
     page breaks," and a page with zero dependency on the app's own static assets or
     JS is the simplest way to guarantee that, independent of whether home base's own
     server is reachable when it's printed.
- **Awards presentation mode (`/awards/present`) reuses `presentation.css` /
  `presentation.js` almost unchanged — see the detailed "Presentation mode..."
  entries above (a pre-Aug-2026-spec session already designed and built exactly this:
  standalone document, curtain reveal-then-advance, judge-sheet overlay, `clamp()`
  sizing verified at both 1280x720 and 1920x1080, native drag-and-drop reorder). That
  session's `services/awards.py` + `awards/present.html` were deleted in the HB1
  reconciliation along with the rest of the pre-spec awards model, but the static
  assets were out of that reconciliation's scope and survived untouched. This task
  only had to write a new `awards/present.html` (the markup the surviving JS/CSS
  already expected via its `.pres-*` classes and `data-car-photo-url`/
  `data-judge-sheet-url` attributes) and a route in `web/awards.py` assembling slide
  data from the current nomination-based award model — no `services/awards.py`
  needed reviving, since `award_results.py`/`awards_setup.py` already cover
  everything a slide needs. Re-verified visually with real Playwright screenshots at
  both resolutions, not just by inspection.
- **The presentation walk reuses `Award.sort_order`** (the field the host already
  drags to reorder on the Awards setup list, via the existing `/shows/{id}/awards/
  reorder` drag-list from HB3) **as the ceremony order** — same trade-off already made
  for `JudgingCategory.sort_order` doing double duty as tie-break priority (see the
  tiebreak_priority entry above).
- **Only Show Award winners are walked in the presentation, not Top Awards.** A
  10-to-200-car Top Awards list is a ranked table (already served by `/results`), not
  a one-at-a-time reveal ceremony — CONTEXT.md's presentation description is
  specifically "each winning car," singular, one Show Award at a time.
- **F2 — Handheld UI foundation: LVGL on top of the already-verified LovyanGFX driver,
  a full component library, and a leak-preventing screen manager.** This is a large
  entry — see each numbered point below for one piece of it.
  1. **LovyanGFX stays the display/touch driver; LVGL is added as the widget/graphics
     layer on top of it, via a ~15-line flush/read adapter (`src/ui/lvgl_port.cpp`) —
     not a second driver.** `src/display/lcd_config.h`'s RGB-panel timing and GT911
     touch config (porches, pulse widths, the 12MHz pixel clock, I2C address) were
     already sourced from Elecrow's verified example back in F1 (see pins.h's citation)
     and already had a bring-up test (`bringup-display`) confirming a real board draws
     correctly and tracks touch — none of that is touched or re-derived here. LVGL's
     `flush_cb` calls `display::gfx().pushImage(...)`; its `read_cb` calls
     `display::getTouch(...)`. This is "don't hand-roll a display driver" satisfied
     literally: the driver is LovyanGFX, exactly as before.
  2. **LVGL pinned to v8.3.11, not v9.** v9 changed enough of the widget/font/style API
     to be its own migration; v8 is what the vast majority of published ESP32 RGB-panel
     LVGL examples (including the CrowPanel/Elecrow community ones pins.h already cites)
     are written against, so v8 keeps this integration on well-trodden ground.
  3. **LVGL's object heap is PSRAM-backed via `LV_MEM_CUSTOM`, hooked to
     `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`** (`src/ui/lvgl_psram_alloc.cpp`) — every
     widget/style/animation LVGL ever allocates comes from the 8MB PSRAM, never the
     ESP32-S3's ~512KB internal SRAM, which the camera DMA buffers, FreeRTOS task
     stacks, and the WiFi stack all need at the same time. The draw buffers are
     separately PSRAM-allocated too (2 x 800x48px, double-buffered, ~150KB total —
     LVGL's own documented ~1/10-screen-height guideline; see `lvgl_port.cpp`'s
     comment for the tuning knob if requirement 7's latency budget isn't met on real
     hardware).
  4. **`LV_TICK_CUSTOM` is off — `lv_tick_inc()` is called by hand from `loop()`
     against `millis()` deltas, not via `LV_TICK_CUSTOM_INCLUDE "Arduino.h"`.**
     `lv_tick.c` is compiled as plain C; `Arduino.h` is a C++ header (String, Print,
     ...) and doesn't compile when pulled into a `.c` translation unit. Same reasoning
     applies to `LV_MEM_CUSTOM_INCLUDE` — it points at a header with C-linkage-only
     function prototypes (`lvgl_psram_alloc.h`), never at anything C++.
  5. **Fonts are the exact same files Home Base self-hosts**
     (`homebase/app/static/fonts/*.woff2` — Archivo 600/700/800, IBM Plex Sans
     400/500/600), decompressed to `.ttf` via `fontTools.ttLib.woff2` (lv_font_conv
     doesn't accept woff2 input directly) and converted with `lv_font_conv` (the
     standard LVGL font tool, run via `npx`). Reproduction, per font:
     ```
     python -m fontTools.ttLib.woff2 decompress homebase/app/static/fonts/<name>.woff2 -o <name>.ttf
     npx lv_font_conv --font <name>.ttf --size <px> --bpp 4 --format lvgl \
       -r 0x20-0x7E --lv-font-name ui_font_<id> -o firmware/src/ui/fonts/ui_font_<id>.c
     ```
     The intermediate `.ttf` files are NOT committed (derived, byte-reproducible from
     Home Base's already-committed `.woff2` sources) — only the 10 generated `.c` font
     files are. Glyph range is ASCII `0x20-0x7E` only, no LVGL symbol glyphs
     (`LV_SYMBOL_*` live in a different codepoint range these fonts don't cover) — every
     place that would normally use a symbol icon (Back, Delete) uses plain ASCII text
     instead; see `screen_manager.cpp` and `numeric_keypad.cpp`. Exactly 10 (family,
     weight, size) combinations exist, chosen per the component that actually needs
     each one — see `src/ui/fonts/fonts.h` for the full mapping and role of each.
  6. **Daylight Mode's exact hex values are new — DESIGN.md only described it
     qualitatively before this task ("near-black text on a white/very light
     background").** Chosen in `ui/theme.cpp`: a light-gray (not stark white) app
     background to cut glare, white cards, gold and the three status colors kept
     IDENTICAL between themes (brand/semantic colors, not surfaces, so they shouldn't
     shift with ambient light). Not WCAG-measured against these specific new pairings
     the way DESIGN.md's dark-mode table is — flagged here rather than presented as
     verified.
  7. **The theme system is NOT LVGL's built-in theme (`lv_theme_default` etc.) reskinned
     — it's a parallel, from-scratch token system** (`ui::theme`): one `Palette` struct
     per mode, and a fixed set of shared `lv_style_t` objects every component applies
     via `lv_obj_add_style()` — never a raw `lv_color_hex()` call in a screen or
     component. `theme::setMode()` mutates those style objects in place and calls
     `lv_obj_report_style_change(nullptr)`, LVGL's documented mechanism for "every
     object using this style, redraw now" — that's the entire mechanism behind runtime
     theme switching needing zero per-screen code. The one deliberate exception is
     `modal_confirm.cpp`'s dim scrim, which is a literal `black @ 50% opacity` — not a
     DESIGN.md token, called out in a comment at the point of use.
  8. **`src/display/theme.h` (the old raw-RGB565-constant module from F1) is untouched
     and still exists, used only by the isolated `bringup_display.cpp` test** — it
     predates this task and that test's own bring-up verification. `src/ui/theme.h` is
     the new, separate, real system every LVGL-based component uses; the two never
     share values or get confused for each other on purpose.
  9. **Screen manager: only ONE screen's widget tree exists in memory at a time.**
     Pushing a new screen destroys the current one's tree immediately (`lv_obj_del`,
     LVGL's real recursive-free, not a hide); popping reconstructs the previous screen
     fresh from a lightweight `(factory, arg)` pair recorded on a fixed-size stack, not
     from a kept-alive object. This was the deliberate, safer reading of "screens must
     create and destroy cleanly" against the task's own explicit worry (a 768KB
     resident framebuffer + 4MB flash means an accumulation of hidden-but-not-freed
     screens is exactly the kind of leak that only shows up hours into a real show).
     A screen that needs to remember something across being popped-to later owns that
     itself as the `void* arg` passed back into its own factory.
  10. **Every callback-taking component frees its own small heap allocation via the
      widget's own `LV_EVENT_DELETE`** (e.g. `numeric_keypad.cpp`'s `CallbackCtx`,
      `list_row.cpp`'s `RowCtx`) — tied to the SAME `lv_obj_del()` that already
      recursively frees the widget tree, so nothing can leak independently of the
      screen-teardown path #9 relies on.
  11. **Physical verification limits — read before trusting requirement 1's "confirm a
      clean refresh" or requirement 7's performance numbers as done.** Nothing in this
      task was run on real hardware — there is no board attached to this environment,
      same limitation recorded for F1's bring-up code (see "Firmware bring-up code is
      verified by actually compiling it," above). What WAS verified: `pio run` succeeds
      (a clean link) for `bringup-ui`, `handheld`, and all three existing bring-up
      environments — no regression. `bringup_ui.cpp`'s header comment lists exactly
      what a human needs to check on the actual board (tearing, touch latency feel,
      theme persistence across a power cycle, repeated navigation not degrading) —
      this task's requirement 7 ("if the RGB panel plus LVGL config cannot hold that,
      tell me BEFORE we build screens on top of it") is answered honestly: it CANNOT
      be told either way without that hardware check.
  12. **Flash/PSRAM report** (all from real `pio run` size output, not estimated):
      - `bringup-display` (F1 baseline — display + touch, no LVGL): 378,609 B flash
        (12.0% of the 3,145,728 B `huge_app.csv` app partition), 20,188 B static RAM.
      - `bringup-ui` (full UI foundation: LVGL + theme + all 14 components + fonts +
        screen manager + debug menu + component demo screen, SD included for theme
        persistence): 693,197 B flash (22.0% of the app partition; 16.5% of the raw
        4MB flash chip), 72,176 B static RAM (22.0%).
      - UI foundation's own incremental cost (`bringup-ui` minus the `bringup-display`
        baseline, SD init included): ~314,588 B flash (~7.5% of the raw 4MB chip),
        ~51,988 B static RAM.
      - The 10 embedded fonts alone: 111,228 B of that flash total (measured via
        `xtensa-esp32s3-elf-size` on each font's compiled `.o`, not file size) — about
        2.65% of the raw 4MB chip on their own.
      - PSRAM: 153,600 B (150KB) for the two LVGL draw buffers, deterministic/fixed.
        LVGL's own object-heap PSRAM usage is NOT a fixed number — it grows and shrinks
        with however many widgets the current screen has alive, by design (see #9) —
        and can only be measured by querying `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`
        on real hardware, which this session couldn't do. Reported as a known gap, not
        guessed.
      - `handheld` (the integrated target — `main.cpp` doesn't call into `ui::` at all
        yet) links at 437,641 B: smaller than `bringup-ui` because the linker's
        dead-code elimination strips every unreferenced UI component and font when
        nothing calls it — confirms the component library is fully tree-shakeable once
        real screens start selectively using pieces of it, not an all-or-nothing cost.

- **F3 — The real judging flow: local storage foundation (settings, show cache, sync
  state, drafts, the finished-car queue) plus the seven screens from Home through
  Review, wired together by a single in-progress-car session.** See each numbered
  point for one piece of it; see firmware/TESTING.md (new) for the battery-pull
  procedure this task's crash-safety design is built to pass.
  1. **Two crash-safe write primitives now exist, at two different layers.**
     `storage::writeFileAtomic()` (`sd_card.cpp`) is the low-level one: write to
     `path.tmp`, flush, close, reopen and verify the size matches, delete any existing
     `path`, rename the temp file in. It has one accepted gap — a crash in the narrow
     window between deleting the old `path` and renaming the new one in loses the OLD
     value — deliberately accepted only for files that are cheap to reconstruct
     (settings, the show cache, sync state). `storage/pending_queue.h`'s finished-car
     queue sidesteps that gap entirely at the design level: every finished car gets its
     own uniquely-named file (`{entry_number}_{closed_at_uptime_ms}.json`), written
     exactly once and never overwritten, so the "delete-then-rename" gap never applies
     to it at all — see that file's own header comment for the full reasoning.
  2. **Drafts (`storage/drafts.h`) are "a QueuedCar that isn't finished yet," reusing
     `pending_queue.h`'s `QueuedScore` struct and count constants rather than a parallel
     definition.** One file per entry number at `/drafts/{entry}.json`, saved via the
     same `writeFileAtomic()` path, after every screen transition in the judging flow —
     see point 4 for why autosave-everywhere was chosen over an explicit save action.
  3. **`storage::checkQueueIntegrity()` classifies every file under `/queue/` into
     valid / orphaned-`.tmp` / unreadable, and only ever acts on the `.tmp` bucket
     (deletes it).** An `unreadable` file (parses as neither a valid queued car nor a
     recognizable `.tmp`) is left untouched and reported — this function is a diagnostic
     that makes the atomic-write design's own claim ("a `.tmp` is never a lost car")
     verifiable rather than assumed, not a repair tool. `bringup-storage` exercises it
     every run; TESTING.md's battery-pull procedure is what actually interrupts a write
     to try to produce an `unreadable` file (it shouldn't be possible to, by design).
  4. **Resume, not discard-or-keep.** A judge who starts a car and walks away before
     Review does NOT get a "discard or keep?" prompt when they come back — that prompt
     is itself a way to lose work (a tired judge taps the wrong button). Instead:
     `judging::save()` (`ui/judging_session.cpp`) writes the draft after every
     meaningful change and on leaving every screen in the flow, recording
     `furthestStep`; `ENTER CAR` (`enter_car_screen.cpp`) checks `storage::hasDraft()`
     for whatever entry number gets typed and, if one exists, resumes straight at
     `furthestStep` via `enterJudgingFlowAt()` — never restarting at Vehicle Details.
     The only explicit "throw this away" path is `judging::discard()`, wired to a
     judge's deliberate action, distinct from just navigating Home.
  5. **A car already finished on THIS device refuses a second attempt, at ENTER CAR
     time, before any screen is even pushed.** `storage::isEntryQueued()` scans
     `/queue/` filenames for `{entry}_*` and, if a draft doesn't already exist for that
     number (i.e. this isn't a resume), blocks with a toast rather than letting the
     judge re-judge and create an on-device duplicate. Cross-device duplicates (two
     different handhelds judging the same car) are explicitly NOT handled here — that's
     Home Base's sync-time conflict resolution, per PROTOCOL.md; this is only the
     cheap, local, same-device case.
  6. **Review's Confirm button is disabled outright (not just refused on tap) until
     both `DraftCar::carPhotoSaved` and `sheetPhotoSaved` are true**, with a plain
     on-screen explanation ("Both photos are required...") rather than a silent
     disabled state — matches CONTEXT.md's photo-required rule and LANGUAGE.md's
     "never a silent dead end" standard. `judging::finish()` itself is the second,
     independent gate: it only deletes the draft and enqueues the car if
     `storage::enqueueCar()` returns a verified-complete write; a failed enqueue
     leaves the draft (and the judge's progress) untouched, and Review shows an error
     toast instead of navigating away.
  7. **`LV_USE_SJPG` flipped on in `lv_conf.h` (was `0`) — the one LVGL feature-flag
     change since F2.** Needed so the Photos screen's preview can decode the JPEG
     `camera::captureToFile()` just wrote straight from a PSRAM buffer, with no LVGL
     filesystem driver involved. Review's own thumbnails take a different, cheaper path
     — `LV_IMG_CF_RAW` fed directly from a `heap_caps_malloc`'d PSRAM buffer — so SJPG's
     flash cost is paid once, by Photos, not twice.
  8. **`camera::CAPTURE_MODE_PHOTO` (1280x720, `CAM_IMAGE_MODE_HD`) is a new, distinct
     mode from the bring-up test's `CAM_IMAGE_MODE_QVGA` (320x240)** — QVGA was chosen
     there only for a fast wiring-check round trip, never meant to represent real photo
     quality. NOT verified against real hardware at 720p specifically (capture time,
     file size, SD write duration) — flagged as an open item below, not assumed fine.
  9. **Bug fixed during this session's own build verification, not part of the original
     task scope: `sd_card.cpp`'s `ensureDir()` only ever called `SD.mkdir()` on the full
     path, while its own header comment promised walking and creating each `/`-segment
     in turn** (true nested creation, since this SD library's `mkdir()` doesn't nest on
     its own). Every CALLER today only ever passes a single-level path (`/drafts`,
     `/queue`), so this was latent, not currently triggered — but the comment was a
     false promise to the next caller that passes something like `/photos/unmatched`.
     Fixed to actually walk segments; re-verified `bringup-storage` and `bringup-judging`
     both still build and link clean after the fix.
  10. **Flash/RAM report** (real `pio run` size output, after the `ensureDir` fix above):
      - `bringup-storage` (settings/show-cache/sync-state/drafts/queue smoke test, no
        UI): 457,549 B flash (14.5% of the app partition), 21,280 B static RAM (6.5%).
      - `bringup-judging` (the full F3 flow, LVGL + all screens + camera + storage):
        735,245 B flash (23.4% of the app partition), 73,352 B static RAM (22.4%) — for
        comparison, F2's `bringup-ui` (LVGL foundation alone, no judging screens) was
        696,253 B flash, so F3's seven screens plus storage layer cost roughly 39,000 B
        of flash on top of the UI foundation.
      - `handheld` (integrated target — `main.cpp` still doesn't call into `ui::` or
        this task's storage modules): 440,669 B flash, 21,776 B static RAM — barely
        above F2's number, confirming F3's additions are still fully tree-shaken out
        until `main.cpp` actually starts calling into them.
  11. **Physical verification limits — same caveat as every prior firmware task.**
      Nothing here ran on real hardware; there is no board attached to this
      environment. What WAS verified: all seven PlatformIO environments (`bringup-ui`,
      `bringup-display`, `bringup-sd`, `bringup-camera`, `bringup-storage`,
      `bringup-judging`, `handheld`) build and link clean, no regressions. New
      `firmware/TESTING.md` documents exactly what a human must physically check —
      including the battery-pull procedure — before this task's crash-safety claims
      (points 1-6 above) can be trusted rather than just argued for.

- **F4 — Vehicle make/model lookup: flash-mapped seed database, a learned-store stub
  for a sync module that doesn't exist yet, Recently Used, a no-punctuation Year
  keypad, and the Make/Model selector screens.** This entry is a revision record as
  much as a design one — the first plan draft for this task was reviewed and sent
  back with five changes before implementation; all five are folded into the points
  below rather than listed separately.
  1. **Partition table: `app0` untouched at 3MB; the old unused `spiffs` allocation
     (896KB, never actually used — photos live on SD, not internal flash) is
     reclaimed for a single new `vehicle_seed` partition, sized to the REAL generated
     binary, not a guess.** `firmware/tools/build_vehicle_seed.py` produced 134,648B
     for the actual sourced dataset (423 makes, 9,176 models — see point 4); rounded
     to the required 4KB flash-sector alignment, that's 0x21000 (135,168B). The
     custom `partitions.csv` (replacing `huge_app.csv`, referenced from
     `platformio.ini`) carves exactly that out of the old spiffs region at its same
     offset (0x310000) and leaves the remaining 0xBF000 (782,336B, ~764KB)
     deliberately UNALLOCATED — not pre-claimed by anything in this task. `nvs`,
     `otadata`, `app0`, and `coredump` are byte-identical to the stock `huge_app.csv`
     table F1-F3 used; confirmed via `gen_esp32part.py` decoding the actual compiled
     `partitions.bin`, not just eyeballing the CSV. The learned store (point 3) uses
     this SAME untouched `nvs` partition — nothing else in the project uses NVS yet,
     so its full 20KB was already free, and no size change was needed there either.
     This whole partition section is a direct reversal of the first plan draft, which
     proposed shrinking `app0` to 2.5MB based on how much of it F3's build actually
     used — correctly rejected on review: today's firmware is a fraction of the
     finished product (sync, battery management, and more remain Open), so sizing
     permanent flash layout against a snapshot of an unfinished build was the wrong
     basis. Zero bytes come out of `app0` under this design.
  2. **The seed dataset is sourced, not hand-invented, with per-entry provenance
     carried in the data itself, not just a commit message.** Two documented layers,
     merged by `firmware/tools/merge_seed_source.py` into
     `firmware/tools/vehicle_seed_source.json`:
     - **NHTSA vPIC** (`vpic.nhtsa.dot.gov`, a U.S. government API — public domain,
       17 U.S.C. 105), fetched live by `firmware/tools/fetch_vpic.py`:
       `GetMakesForVehicleType` for Car/Truck/Multipurpose Passenger Vehicle
       (406 unique makes), then `GetModelsForMake` for every one of them (9,124
       models, all model years vPIC has). vPIC's own Motorcycle type (1,684 makes)
       was fetched and spot-checked, then DELIBERATELY EXCLUDED — dominated by
       one-off custom/chopper builders ("MOONLIGHT CHOPPERS," "CRAZY DAGO CUSTOMS," a
       few names in) rather than the recognizable manufacturers a car-show Best Bike
       nomination actually needs.
     - **Curated-classic supplement** (`firmware/tools/curated_classic.json`) for
       what vPIC under-covers: pre-1980s/discontinued American makes (vPIC's model
       catalog is strongest from roughly the 1980s on) and a genuinely motorcycle-only
       make list (Harley-Davidson, Indian, Ducati, etc. — vPIC's own motorcycle data
       being unusable per above). Every model name in this layer was cross-checked
       against that make's real Wikipedia page by
       `firmware/tools/verify_curated_classic.py` (a live fetch + substring match, not
       an assertion) before inclusion — only the bare FACT "this make produced a model
       with this name" is used, never Wikipedia's own prose, which stays CC BY-SA and
       out of the shipped binary entirely. 152/154 names matched verbatim on their
       make's consolidated list page; the remaining 2 (Chevrolet Delray, BMW R nineT)
       were confirmed via their own dedicated Wikipedia pages instead, since some
       older/niche models have a standalone page rather than a line in the
       consolidated list — noted here rather than silently waved through.
     - A curated motorcycle make that's ALSO a car/truck make (Honda, BMW, Suzuki,
       Triumph — all already present via vPIC) has its motorcycle models MERGED into
       that SAME make entry by `merge_seed_source.py`'s case-insensitive name match —
       deliberately never a second "Honda" alongside a "Honda Motorcycle."
     - 58 model names from vPIC's own raw data were dropped during merge (logged, not
       silently discarded) for failing the ASCII/47-byte checks
       `storage::DraftCar.make/model`'s `char[48]` fields require — almost entirely
       vPIC data-quality noise from its broader VIN-decode registry (upfitter/trailer
       manufacturer names mis-grouped under a car make, e.g. "Golden Eagle Trailer
       Manufacturing & Welding Works" under "EAGLE," plus a couple of non-ASCII
       characters in Land Rover model codes) — not curated-data casualties.
  3. **The learned store is real code with nowhere to be called from yet, and that's
     the honest state, not a stub pretending otherwise.** `storage::vehicle_db`'s
     `saveLearnedVehicle()` writes into a `Preferences` (NVS) blob under namespace
     `veh_learn`, key `models` — a single JSON array `[{make, model}, ...]`, not one
     key per make, because ESP32 NVS keys cap at 15 characters and several real make
     names (e.g. "International Harvester") don't fit. Nothing in this codebase calls
     `saveLearnedVehicle()` yet: PROTOCOL.md's `vehicle_additions` is meant to arrive
     over WiFi sync, and `network/wifi_sync.h` doesn't exist (still Open). Every query
     function (`findMakes`/`findModels`/`modelBelongsToMake`) already merges the
     learned store in regardless — on a real device today it will simply always be
     empty until a future sync task starts writing to it.
  4. **Recently Used is scoped by Show ID, not Show Name — a real fix from plan
     review, not an original design.** `storage::ShowInfo` gained `int showId` (Home
     Base's `Show.id`) and `char eventDate[11]` / `int eventYear` (from
     `Show.event_date`) — see point 5. PROTOCOL.md's `configuration` example now
     documents `show_id` and `event_date` alongside the fields it already had; this is
     a DOCS-ONLY change on the Home Base side — no `homebase/` code touched, since the
     sync endpoint that would actually serialize this doesn't exist yet either (same
     "not yet wired, honestly stated" situation as point 3). `storage::vehicle_recents`
     stores the show id a `/vehicle_recents.json` file was built under; a mismatch
     against the CURRENT `ShowInfo.showId` means "new show, not just a renamed one,"
     so the recents reset — a plain string comparison on `show_name` would have missed
     exactly that case (a show renamed mid-event, or two shows that happen to share a
     name).
  5. **Year's upper bound is the show's own event year + 1, not a hardcoded future
     date — also a plan-review fix.** `ShowInfo::eventYear` is parsed once, at
     `loadShowInfo()` time, from `eventDate`'s first 4 ASCII digits (a deliberately
     tiny hand-rolled parse, not a date library — see `show_data.cpp::parseYear`).
     Range: `[1885, eventYear + 1]`. This is still fully offline and RTC/NTP-free —
     `event_date` is DATA Home Base already has and pushes down at sync time, not a
     live clock reading, so it needs nothing beyond "a sync has happened at least
     once," the same precondition Categories/Awards already carry. Pre-first-sync
     (`eventYear == 0`), the range falls back to a wide `[1885, 2035]` rather than
     rejecting every input — flagged in `vehicle_details_screen.cpp` as the
     PRE-SYNC-ONLY path, never the normal one.
  6. **`numeric_keypad_overlay` is a NEW component, not a reuse of
     `numericKeyboardOverlay`** (`text_keyboard.h`) — the latter's `lv_keyboard`
     NUMBER_MAP includes a decimal point, which fails the task's explicit "no
     punctuation" requirement for Year. The new overlay wraps
     `components::numericKeypad` (already existed, digits + Backspace only) in the
     same modal chrome `text_keyboard.cpp`'s overlay uses (field visible above,
     Cancel leaves the prior value untouched, self-deletes after Done/Cancel), adding
     `autoAcceptLen` + a `Validator` callback: typing the 4th digit of a plausible
     year closes the overlay with no Done tap needed; an implausible 4th digit leaves
     it open, Done disabled, with an inline hint — both the auto-accept path and the
     manual Done fallback are gated by the exact same validity check, so there's no
     way to bypass one but not the other.
  7. **The Make/Model selector is a NEW purpose-built component
     (`vehicle_selector_list`), not an adaptation of the existing
     `searchable_selector.h`.** The two diverge too much to share: `searchable_selector`
     holds a flat in-memory `const char**` and only refilters after a separate
     keyboard OVERLAY's Done tap; this task needs an INLINE, always-on-screen keyboard
     that refilters on every keystroke (task item 4.1 — "filtering as they type"), a
     merged seed+learned data source instead of a plain array, and a Recently Used
     section. `searchable_selector.h` is untouched, still demo-only
     (`component_demo_screen.cpp`'s only caller). The inline keyboard reuses
     `text_keyboard.cpp`'s plain-ASCII key map and key-handling logic directly — both
     were factored out into `plainTextKeyMap()` / `keyPressedIntoTextarea()` on
     `text_keyboard.h` rather than duplicated.
  8. **Search is a full bounded linear scan, not a binary-search-narrowed one — a
     deliberate refinement of the plan's own wording, not what got implemented
     blindly.** The plan described "binary search plus a bounded scan"; building it
     honestly required admitting binary search can't help a SUBSTRING match ("vette"
     finding "Corvette" could land anywhere in a sorted table, not just where a prefix
     comparison would point) — only a full scan is correct for that. With ~423 makes
     and each make's own model range small, a full scan costs well under a
     millisecond regardless, so nothing is lost. Binary search IS used, correctly,
     for the one place it actually helps: `findExactMakeIndex()`, an EXACT lookup
     against the sorted make table, which backs `modelBelongsToMake()` (point 9) and
     the Make->Model navigation rule (point 9).
  9. **Make -> Model navigation auto-advances exactly when the task's revised
     instructions describe, via `screen_manager::pop()` immediately followed by
     `push(ModelSelectorScreen::create)` inside the same click handler.** Both calls
     are synchronous stack mutations that happen before LVGL's own render tick runs
     (see F2's screen-manager point 9: "destroy immediately, rebuild immediately"),
     so there's no visible flash of Vehicle Details in between and the net stack
     depth is identical either way — `ModelSelectorScreen`'s own completion path
     never needs to know whether it was reached by auto-advance or a direct tap; it's
     always a plain `pop()`. Rule: Model blank -> advance; Model set but no longer
     valid under the newly-chosen Make (checked via `modelBelongsToMake`, point 8) ->
     clear it, advance; Model set and still valid (including "Make didn't actually
     change") -> leave it, return to Vehicle Details.
  10. **Flash/RAM report** (real `pio run` size output): `bringup-judging` (F3's full
      flow plus this task's screens/storage/vehicle_db) now links at 756,177B flash
      (24.0% of the still-3MB `app0` — up from F3's 735,245B, so this task's own
      addition to `app0` is ~20,932B, negligible next to the headroom point 1
      preserved) and 81,592B static RAM (24.9%). `handheld` (the integrated
      target, `main.cpp` still not calling into any of this) is 443,181B flash,
      barely above F3's 440,669B — confirms this task's additions are still fully
      tree-shaken out until `main.cpp` actually calls into them, same pattern F2/F3
      already established.
  11. **Physical verification limits — same caveat as every prior firmware task, plus
      two more that are specific to this one.** Nothing here ran on real hardware.
      Beyond the usual (touch/search feel, theme, refresh quality — see
      `firmware/TESTING.md`'s new F4 section), TWO further gaps that are inherent to
      this environment, not just "not gotten to yet": `pio run -t
      upload-vehicle-seed` (`tools/upload_vehicle_seed.py`) registers correctly and
      fails with PlatformIO's own standard "no upload port" error when actually run
      here — confirming the target mechanism works, but the real `esptool` flash
      write to a live board is unverified; and the vPIC/Wikipedia fetches (points 2)
      required live internet access, which this dev environment happened to have —
      if a future session regenerates the seed data without it, `fetch_vpic.py` and
      `verify_curated_classic.py` will simply fail loudly (network errors), never
      silently produce bad data.

- **F5 — Handheld WiFi sync: scan-first, backoff-aware, never-lose-a-queued-car,
  against Home Base's already-live `POST /api/v1/sync`.** `firmware/src/network/
  wifi_sync.h`'s stale placeholder (referencing the old two-endpoint protocol) is
  replaced with the real thing. See each point below for one design decision.
  1. **Confirmed against the ACTUAL running Home Base code, not just PROTOCOL.md's
     prose, before writing a line of firmware — and found one real drift.**
     `homebase/app/api/schemas.py`'s real `ConfigurationOut` does NOT include
     `show_id`/`event_date` yet, despite PROTOCOL.md's example JSON showing them
     (added in F4's entry as a documented, docs-only, aspirational change with no
     homebase code behind it). `wifi_sync.cpp` parses both defensively (`config
     ["show_id"] | before.showId`, ArduinoJson's standard default operator) — they
     simply stay whatever they already were until Home Base's schema catches up to
     its own documentation. Everything else (the four `results[]` status values,
     server-side range conversion behavior, the total absence of any "Update Now"
     concept server-side) matched PROTOCOL.md exactly.
  2. **The background task touches NEITHER LVGL NOR storage — not even to build its
     own request body.** The original plan draft had the task doing "WiFi scan,
     WiFi connect, HTTP POST" without being explicit about who builds the request
     JSON; implementing it honestly surfaced a real concurrency hazard: if the task
     read `storage::Settings`/`SyncState`/the queue itself, that's SD/SPI access
     that could race with the MAIN thread also touching the SD card at the exact
     moment a judge is saving a draft or a photo — this module deliberately does
     NOT block the main thread the way `camera.cpp`'s poll-loop precedent does (see
     point 3), so that race is real, not theoretical. Fix: `buildRequestBody()`
     (all storage reads) runs on the MAIN thread, synchronously, in the same call
     that triggers a sync attempt — fast (a few small file reads), and finishes
     before the task is even created. The task receives an already-serialized JSON
     buffer and a URL; it does scan/connect/POST/read-response and NOTHING else.
     Applying the result (`applyHit()`/`applyMiss()` — every storage write, every
     status bar/toast/screen call) happens back on the main thread too, from a
     short-period (150ms) LVGL timer polling a `volatile bool done` flag — the same
     ownership split as `camera.cpp`'s `CaptureContext`, just with the boundary
     drawn to keep ALL storage/LVGL access single-threaded, not just LVGL.
  3. **Deliberately NOT `camera.cpp`'s pattern of the caller blocking (via a
     `delay()` poll-loop) until the task finishes — a real, reasoned deviation from
     this project's own established precedent, not an oversight.** Camera capture is
     always a judge-initiated, foreground wait (they're looking at the Photos
     screen, expecting it to take a moment). Trigger (b) — the periodic timer — is
     the opposite: it fires unprompted while the judge could be mid-keystroke on any
     screen. Blocking the single LVGL thread for the 1-5 seconds a real scan+connect
     +POST can take would freeze touch input at an unpredictable moment, which is a
     worse regression than the camera's contained, expected wait. Hence the
     completion-poll-timer design in point 2 instead of a blocking wait.
  4. **`storage::SyncState::currentRetryIntervalSeconds` (already existed, unused
     until now) is the ONLY thing the periodic trigger ever mutates** — doubles
     (capped at 900s/15min) on a miss, resets to `Settings.syncIntervalSeconds` on a
     hit. Triggers (a)/(c) (`network::sync::requestNow()`) run the identical routine
     but NEVER touch this value either way, on a hit OR a miss — matching the task's
     explicit "never apply backoff to a just-finished-car trigger," extended to
     Update Now by the same reasoning (a judge manually checking shouldn't
     inappropriately double the periodic schedule just because they happened to
     check while still out of range). A SUCCESSFUL sync from ANY trigger still
     resets the interval to base — reaching Home Base is real information the
     periodic schedule should reflect regardless of what caused the attempt.
  5. **`Settings.syncIntervalSeconds` is new** (default 180s, matching PROTOCOL.md's
     documented default) — the task's own "from settings, not hardcoded"
     instruction. Exposed on Settings screen in whole minutes (1-15; validated, not
     silently clamped — a base at or above the 15-minute cap would never actually
     back off, which would silently defeat the whole point of a configurable base).
  6. **Status vocabulary: the status bar's existing terse pill words (`UP TO DATE`/
     `UPDATING`/`NOT CONNECTED`) are UNCHANGED** — no added ellipsis on "Updating"
     despite the task listing "Updating..." — because LANGUAGE.md's own recurring-
     state-phrasing rule ("a judge who learns what 'Updating' means on the handheld
     must see the identical word... on Home Base's screen") is a stronger, explicit,
     already-established constraint than the task's shorthand list, and Home Base's
     own dashboard pill (confirmed via `services/dashboard.py`) says "Updating" with
     no ellipsis either. The task's fuller-sentence forms ("Home Base Not
     Connected," "N cars waiting to send," the 20-minute banner) are read as the
     SENTENCE-level phrasing LANGUAGE.md's own table distinguishes from the terse
     status-bar word for the same underlying state — both point at the same state,
     rendered at two different levels of the UI, not five different states.
  7. **`error`-status queued cars are deliberately left in the queue forever, not
     auto-removed and not given resolution UI.** The task's own framing (only
     "explicitly acknowledged" cars leave the queue; `already_recorded` explicitly
     called out as counting, `error` conspicuously not mentioned in that list) reads
     as intentional, and PROTOCOL.md's `error` case is a validation failure the
     handheld already should have prevented client-side before ever enqueueing (an
     unscored active category, an out-of-range point value) — expected to be rare
     to nonexistent in practice. Flagged here as a real gap if it ever DOES happen:
     that car would sit queued and keep getting re-sent every sync attempt, forever,
     with no judge-facing indication anything is wrong. Revisit if this ever
     surfaces at a real show.
  8. **`battery_pct` is sent as an honest JSON `null`, matching `SyncRequest`'s own
     `int | None = None`.** No ADC pin has been identified for this board yet
     (`firmware/src/power/battery.h`, still Open below) — never a fabricated
     percentage standing in for real hardware that doesn't exist yet.
  9. **`server_time` sets the device clock via `strptime()` + `settimeofday()`** —
     no NTP, no network beyond the sync response itself, matching CONTEXT.md's "no
     NTP time sync... handhelds set their clock from home base's `server_time`."
  10. **Flash/RAM report** (real `pio run` size output): `bringup-judging` jumps to
      1,234,117B flash (39.2% of `app0` — up from F4's 756,177B, a ~478,000B
      increase almost entirely from the WiFi/HTTPClient/TLS-adjacent libraries this
      is the FIRST task to actually link in) and 107,600B static RAM (32.8%, up
      from 81,592B). Still comfortably within the 3MB `app0` this project chose to
      leave untouched back in F4 specifically because "today's firmware is a
      fraction of the finished product" — this jump is the concrete proof that
      call was correct: the earlier plan draft that would have shrunk `app0` to
      2.5MB would have left only ~1.27MB of headroom against a component this
      size, a real risk this decision avoided. `handheld` (the integrated target,
      still not calling into `network::` at all) is 459,085B, barely above F4's
      443,181B — confirms this task's additions are still fully tree-shaken out
      until `main.cpp` actually calls into them, the same pattern every prior
      task established.
  12. **Real bug found and fixed while implementing this task, not part of the
      original scope: `judge_car_screen.cpp` was unconditionally overwriting
      `judging::current().scoreRangeMax` from the live show's CURRENT range on
      every single screen build.** Before this task, that was harmless — nothing
      could ever change the show's range at runtime, so the value was always
      identical. This task makes it possible for a sync to escalate the range
      WHILE a judge has a car open on Judge Car (not yet queued, so none of
      PROTOCOL.md's server-side conversion-on-receipt logic has run for it) — the
      old code would have silently relabeled already-entered raw point values onto
      the new scale with no conversion at all, the exact opposite of "the judge
      converts nothing" for a car that was NEVER actually sent yet. Fixed: the
      overwrite now only happens while the draft is still genuinely untouched (no
      scores entered, no Overall Impression) — matching `judging_session.h`'s own
      already-documented rule for exactly this ("only applied when starting fresh,
      since a resumed draft already has whatever range was in effect when it was
      started"), just not previously enforced at this one call site. Known
      remaining gap, not fully solved: if a judge has PARTIAL scores at the old
      range and the show escalates before they finish that same car, the
      persisted draft data is now safe (never silently corrupted), but the
      row-vs-grid layout choice and the on-screen Total denominator for the REST
      of that one screen session still read the show's new range rather than the
      draft's original one — a display-only inconsistency in a narrow window, not
      a data-loss or data-corruption one. Flagged here rather than fully chased
      down, since `score_grid` (the 1-25 layout) has no range parameter at all —
      properly fixing the display would need touching that component's own design,
      out of proportion for an edge case this narrow (a sync landing in the exact
      seconds a judge is on this one screen, mid-car, with a range escalation
      simultaneously in flight).
  13. **Physical verification limits — same caveat as every prior firmware task, more
      acute here than usual.** Nothing in this task can be exercised at all without
      a real board AND a reachable Home Base instance — no board, no AP, no server
      available in this environment. `firmware/TESTING.md`'s new F5 section lists
      ten specific real-hardware checks (backoff doubling/capping, the finished-car
      trigger bypassing backoff, queue draining, the two-handheld-same-car conflict
      path, the Scoring Updated notice, the 20-minute banner) — every one of them is
      currently unverified beyond "compiles and traces correctly against
      PROTOCOL.md/the task spec," which is the most this environment can confirm.

## Open

- **`camera::CAPTURE_MODE_PHOTO` (1280x720) is unverified against real hardware.** F3
  picked `CAM_IMAGE_MODE_HD` for the judging flow's two required photos, but the only
  resolution ever exercised on real hardware was the bring-up test's QVGA (320x240) —
  see F3's point 8. Once a board is available: confirm capture time, resulting JPEG
  file size, and SD write duration are all acceptable for a judge moving quickly
  through 100-400 cars; drop to a lower `CAM_IMAGE_MODE_*` if 720p turns out too slow
  or too large.
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
