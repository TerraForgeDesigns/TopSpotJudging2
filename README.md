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
- [ ] **PlatformIO toolchain + libraries:** open `/firmware` in PlatformIO and let it
      download the ESP32-S3 platform, toolchain, and any libraries the firmware
      depends on. This is a one-time download per machine that builds firmware.
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

1. Power on the GL.iNet GL-SFT1200 router. It creates the isolated show WiFi network
   — no internet uplink needed or expected.
2. Start home base on the Windows laptop, connected to the router.
3. Power on each handheld. Each one is provisioned with the router's SSID and the
   home base's LAN IP ahead of time.
4. Judges score cars through the day; handhelds sync scores opportunistically per the
   trigger model in PROTOCOL.md.
5. After judging closes, connect each handheld to the laptop via USB (preferred) to
   transfer photos, or fall back to WiFi upload if USB isn't available.
6. Use home base to resolve any flagged conflicts, compute results, and run the
   awards presentation.

*(This section will grow with concrete commands once `/homebase` and `/firmware` have
actual code in them — right now this repo is structure and specification only.)*
