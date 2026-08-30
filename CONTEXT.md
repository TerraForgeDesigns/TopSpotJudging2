# CONTEXT.md — Top Spot Judging

Read this first, every session. It has none of the conversation history — it has to stand alone.

## What this is

Top Spot Judging is an offline-capable car show judging system. A show is a single-day
outdoor event with 100–400 entered cars, run with no IT support and no internet access
once the event starts. The system has three physical parts that must interoperate over
an isolated local network:

1. **Home Base** — a Python app running on a Windows laptop at the show. It is the
   source of truth for the entire event: show setup, car roster, judging criteria,
   incoming scores, photo storage, results computation, and the awards presentation
   display.
2. **Handhelds** — 3 to 4 battery-powered ESP32-S3 devices carried by judges walking
   the show field. Each has a SPI touchscreen, an Arducam 5MP SPI camera, a microSD
   card for photo storage, and a 3.7V 4400mAh LiPo battery. Judges use them to enter
   scores and photograph each car plus its paper judge sheet.
3. **Router** — a GL.iNet GL-SFT1200 (Opal) travel router. It creates an isolated LAN
   in a field. It is the only link between handhelds and home base, and it provides
   **no internet** — it is not a gateway to anything, just a local WiFi network.

Both the PC application (`/homebase`) and the ESP32 firmware (`/firmware`) live in this
one repository so that protocol and design changes can be reviewed together.

## The real-world judging workflow

- Every car entered in the show gets a paper registration form with a printed
  **registration number** on it. That number is the human-facing identity of the car
  for the rest of the day.
- A typical show is 100–400 cars. Judges are given a general area of the field to
  cover, but it is "get to them as you can" — there is no system-assigned per-car
  route or queue.
- A judge walks up to a car, collects the paper form, enters the registration number
  on the handheld, scores the car against the judging criteria, photographs the car
  and the paper judge sheet, and closes the car out. That closes the loop for that car
  on that handheld.
- All judges score against the **same** criteria (e.g. Paint, Engine, Interior — see
  Glossary). Each car is judged exactly once, by exactly one judge. There is no
  cross-judging or averaging across judges in this system.
- Judges roam an open field and are **regularly and expectedly** out of WiFi range of
  the router. This is the normal operating condition, not an error state, and the
  entire sync design (see PROTOCOL.md) is built around it.
- **Scores sync over WiFi during the show**, opportunistically, whenever a handheld
  wanders back into range.
- **Photos do not sync during the show.** They transfer only after judging closes,
  via USB (preferred) or WiFi (fallback), while the handheld is plugged into power.
  This is a deliberate choice: photo payloads are too large to fight for airtime with
  score sync on a small travel router while judges are actively working.
- After judging closes, the host uses the home base app to resolve any scoring
  conflicts (see PROTOCOL.md — duplicate handling), compute final results, and run an
  awards presentation that shows each winning car's photo, its judge sheet photo, its
  score breakdown, and the announcer's name.

## Non-negotiable constraints

### 1. Offline-first

After initial development setup, the entire system runs with **zero internet access**.
Concretely:

- No CDN links for CSS, JS, fonts, or icons anywhere. Everything is vendored into the
  repo and served locally.
- No runtime calls to any external API, telemetry endpoint, font service, map service,
  or update check.
- No NTP time sync. The Windows PC clock is the single authoritative time source for
  the whole system. Handhelds set their clock from the home base's `server_time` field
  on every sync response (see PROTOCOL.md).
- Python dependencies are pinned in `requirements.txt` and installed into a local venv
  while internet is still available; the venv is never rebuilt from the network again
  at the event.
- Steps that legitimately require internet happen once, ahead of time, during setup —
  see README.md's offline prep checklist. Nothing at the event itself may require
  internet.

### 2. Modern visual design

Both the PC app and the handheld UI must look contemporary and polished — this is
software that show organizers configure and an audience watches during awards, not an
internal tool. See DESIGN.md for the full system. Explicitly ruled out: default browser
form styling, gray beveled buttons, Windows 95/98/XP-era chrome, clip art, Comic Sans.

### 3. Resilience over cleverness

This system runs once a day, outdoors, with no IT support on site. Failures must be
**visible and recoverable, never silent**. Data already entered by a judge must survive
a crash, a battery pull, or a handheld that never comes back into range before the show
ends. When in doubt, favor the boring, debuggable, restart-safe option over a clever one.

## Hardware list

| Component | Spec |
|---|---|
| Home base | Windows laptop, runs the Python app |
| Handheld MCU | ESP32-S3 |
| Handheld display | SPI touchscreen |
| Handheld camera | Arducam 5MP, SPI interface |
| Handheld storage | microSD card (photo storage, offline queue, preferences) |
| Handheld power | 3.7V 4400mAh LiPo battery |
| Router | GL.iNet GL-SFT1200 ("Opal") travel router, isolated LAN, no internet uplink |
| Handheld quantity | 3–4 concurrent judges per show |

Exact touchscreen and Arducam part numbers are not yet chosen — tracked as an OPEN item
in DECISIONS.md, needed before firmware display/camera work starts.

## Glossary

These terms get confused easily — use these definitions precisely in code, docs, and
conversation.

- **Judging Criteria** — the scoring rubric categories (e.g. Paint, Engine, Interior).
  Identical for every car in the show. This is what a judge actually scores a car
  against on the handheld.
- **Car Class** — an award grouping (e.g. "1970s Muscle", "Trucks", "Stock"). Used to
  determine "Best in Class" awards. **Independent of the scoring rubric** — a car's
  class does not change which criteria it's judged on, only which other cars it
  competes against for awards.
- **Registration Number** — the unique number printed on a car's paper form at
  check-in. The primary key humans use to refer to a car all day. It is also the
  photo-matching key: photo filenames embed it (see PROTOCOL.md).
- **Handheld** — one judge's ESP32-S3 device, including its display, camera, SD card,
  and battery.
- **Home Base** — the Windows laptop running the Python app; the source of truth for
  the whole show.
- **Sync Window** — a brief period during which a handheld has WiFi radio on and is
  actively exchanging data with home base (roster pull, submission push). Outside a
  sync window, a handheld's WiFi radio is off to save battery and it is operating
  entirely from its local queue and cached roster.

## Related documents

- [DESIGN.md](DESIGN.md) — visual design system for both platforms.
- [PROTOCOL.md](PROTOCOL.md) — the network and data contract between handheld and home base.
- [DECISIONS.md](DECISIONS.md) — running log of architectural decisions and open questions.
- [README.md](README.md) — setup, running, and the offline prep checklist.
