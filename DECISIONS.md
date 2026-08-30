# DECISIONS.md — Architectural Decision Log

Running log of decisions made and questions still open. Update this whenever a real
architectural choice gets made or reversed — this is the file future sessions check to
avoid re-litigating settled questions, and to know what's still unresolved before
building on top of it. Newest entries at the top of each section.

## Decided

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
