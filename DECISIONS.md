# DECISIONS.md — Architectural Decision Log

Running log of decisions made and questions still open. Update this whenever a real
architectural choice gets made or reversed — this is the file future sessions check to
avoid re-litigating settled questions, and to know what's still unresolved before
building on top of it. Newest entries at the top of each section.

## Decided

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
