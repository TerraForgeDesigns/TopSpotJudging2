# Top Spot Judging

An offline-capable car show judging system: a Python home base app, ESP32-S3 judge
handhelds, and an isolated field router. See [CONTEXT.md](CONTEXT.md) for the full
project overview and workflow, [DESIGN.md](DESIGN.md) for the visual design system,
[PROTOCOL.md](PROTOCOL.md) for the handheld↔home base network contract, and
[DECISIONS.md](DECISIONS.md) for the architectural decision log.

## Repository layout

```
/CONTEXT.md      project overview, workflow, constraints, glossary
/DESIGN.md       visual design system (web + handheld)
/PROTOCOL.md     network + data contract between handheld and home base
/DECISIONS.md    architectural decision log + open questions
/README.md       this file
/homebase/       Python PC application (FastAPI + SQLite)
/firmware/       ESP32-S3 firmware (PlatformIO + Arduino framework)
/docs/           diagrams and reference material
```

## Offline prep checklist

This system runs with **zero internet access at the event**. Everything below that
needs internet must be done ahead of time, while you still have a connection.

### Needs internet — do this once, ahead of the event

- [ ] **Python deps:** `pip install -r homebase/requirements.txt` into a local venv.
      Once installed, the venv is never rebuilt from the network again.
- [x] **PlatformIO toolchain + libraries:** `cd firmware && pio run -e handheld`
      downloads the ESP32-S3 platform, toolchain, and both libraries (LovyanGFX,
      Arducam_Mega — pinned exact versions, see `firmware/platformio.ini`) into
      `.pio/`. Already done once in this repo as of the F1 hardware bring-up session
      — all four environments build successfully offline from here. Re-run only if
      `.pio/` gets deleted or a dependency version changes.
- [ ] **Font files:** download the Archivo and IBM Plex Sans/Mono `.woff2` files and
      place them in `homebase/app/static/fonts/` (see DESIGN.md typography section).
      They are vendored into the repo, not linked from a font service, so this is a
      one-time fetch, not a runtime dependency.
- [ ] **Handheld display/camera fonts:** convert Archivo and IBM Plex Sans to the
      embedded display library's font format at the sizes specified in DESIGN.md, and
      commit the generated font files into `/firmware`.

### Needs internet at the event

**Nothing.** Once the steps above are done and committed, home base, the handhelds,
and the router form a closed loop with no calls out. If you ever find code that
reaches out to a CDN, a font service, an NTP server, or any other external endpoint at
runtime, that's a bug against the offline-first constraint in CONTEXT.md — fix it, don't
work around it.

## Running the system (event day)

Full, concrete instructions live in two docs written for whoever is actually
running the show, not necessarily whoever built it:

- [docs/router-setup.md](docs/router-setup.md) — one-time GL.iNet GL-SFT1200
  setup: the show Wi-Fi network, the fixed DHCP reservation that lets every
  handheld find Home Base with zero manual configuration, what to turn off, field
  placement, and recovery if the router loses power mid-show.
- [docs/show-day-runbook.md](docs/show-day-runbook.md) — the full day-of sequence
  (starting Home Base, checking handhelds) and troubleshooting for the failures
  that actually happen at a show: a handheld that won't connect, photos that won't
  import, a car judged twice, a wrong entry number, a handheld that died with work
  unsent, a vehicle the built-in list doesn't know.

The short version: power on the router, start Home Base on the laptop
(`.venv\Scripts\python -m uvicorn app.main:app --host 0.0.0.0 --port 8000`), power
on each handheld and confirm it reads **Up to Date**, judge the show, then pull
photos off each handheld's microSD card (or fall back to Wi-Fi upload) and use
Home Base to resolve conflicts, compute results, and run the awards presentation.
