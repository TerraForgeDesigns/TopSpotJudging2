# CONTEXT.md — Top Spot Judging

Read this first, every session. It has none of the conversation history — it has to stand alone.

## What this is

Top Spot Judging is an offline-capable car show judging system. A show is a single-day
outdoor event with 100–400 entered cars, run with no IT support and no internet access
once the event starts. The system has three physical parts that must interoperate over
an isolated local network:

1. **Home Base** — a Python app running on a Windows laptop at the show. It is the
   source of truth for the entire event: a guided Show Setup wizard, the car roster
   (entry numbers generated automatically, details filled in as judging happens),
   incoming scores, photo storage, results computation, awards (automatic Top Awards
   and configurable Show Awards), and the awards presentation display.
2. **Handhelds** — 3 to 4 battery-powered ESP32-S3 devices carried by judges walking
   the show field. Each has a touchscreen, an Arducam 5MP SPI camera, a microSD card
   for photo storage, and a 3.7V 4400mAh LiPo battery. Judges use them to enter scores
   and photograph each car plus its paper judge sheet.
3. **Router** — a GL.iNet GL-SFT1200 (Opal) travel router. It creates an isolated LAN
   in a field. It is the only link between handhelds and home base, and it provides
   **no internet** — it is not a gateway to anything, just a local WiFi network.

Both the PC application (`/homebase`) and the ESP32 firmware (`/firmware`) live in this
one repository so that protocol and design changes can be reviewed together.

## The real-world judging workflow

- At show creation, the organiser tells Home Base how many cars the show will have.
  Home Base generates that many sequential **entry numbers** up front — 001, 002, ...,
  zero-padded to three digits. There is no roster import of any kind: entries start
  **empty**, with no participant or vehicle details attached.
- A typical show is 100–400 cars. Judges are given a general area of the field to
  cover, but it is "get to them as you can" — there is no system-assigned per-car
  route or queue.
- A judge walks up to a car and enters its entry number on the handheld. If Home Base
  already holds details for that entry number — filled in earlier by another judge, or
  corrected by the host — the handheld pre-fills Participant, Year, Make, and Model and
  the judge confirms them; otherwise the judge enters them fresh, and they upload along
  with the scores. Whoever reaches an entry first is who fills it in. Home Base can
  view and correct any entry's details at any time.
- More cars can be added to the show at any time. Adding cars assigns the next
  available entry numbers — it never renumbers existing entries and never affects
  existing scores.
- All judges score against the **same** active Judging Categories (see "Judging
  categories & scoring range" below). Each car is judged exactly once, by exactly one
  judge. There is no cross-judging or averaging across judges in this system.
- Judges roam an open field and are **regularly and expectedly** out of WiFi range of
  the router. This is the normal operating condition, not an error state, and the
  entire sync design (see PROTOCOL.md) is built around it.
- **Scores sync over WiFi during the show**, opportunistically, whenever a handheld
  wanders back into range.
- **Photos do not sync during the show.** They transfer only after judging closes,
  via USB (preferred) or WiFi (fallback), while the handheld is plugged into power.
  This is a deliberate choice: photo payloads are too large to fight for airtime with
  score sync on a small travel router while judges are actively working.
- After judging closes, the host uses Home Base to resolve any scoring conflicts (see
  PROTOCOL.md), compute final results (with tie-breaking — see below), choose winners
  for any award that needs a manual decision, and run an awards presentation that shows
  each winning car's photo, its judge sheet photo, its score breakdown, and the
  announcer's name.

## Show setup

Show creation is a guided, six-step wizard, each step showing only itself, with Back
and Continue preserving everything already entered when moving backward:

```
Show Details -> Number of Cars -> Judging Setup -> Awards -> Review -> Create Show
```

After creation, the organiser lands on the Show Dashboard. Its sections, exactly these
names: **Overview, Cars, Judging, Handhelds, Awards, Photos, Results, Edit Show.**

## Judging categories & scoring range

There are five built-in Judging Categories: **Engine, Exterior, Interior, Paint,
Wheels / Tires.** Each can be turned on/off and renamed independently. New categories
cannot be added — this is a closed, fixed set by design. Only active categories appear
on handhelds, count toward totals, and count toward Max Score.

The scoring range is never chosen by the organiser — Top Spot sets it from the number
of cars in the show:

| Cars in show | Score range per category |
|---|---|
| 1–150 | 1–5 |
| 151–300 | 1–10 |
| 301+ | 1–25 (open-ended — 501+ still uses 1–25) |

Show Setup states this as information, never as a setting — e.g. "150 cars — judges
will score each category from 1 to 5", "Max Score: 25".

**The range only ever moves up.** If the car count later falls below a threshold
(cars removed), the range does not drop back down — once a level is crossed, it holds
for the rest of the show. Removing a car must never recalculate other cars' scores
downward.

**Max Score** = (number of active categories) × (range maximum). Always computed,
never entered directly.

Every category shown to a judge must be scored — there is no zero and no
"not applicable." Minimum is 1.

## Score conversion when the range escalates

If the show's range escalates after a car was already scored at a lower range, that
car's per-category scores are converted, never left at the old scale:

```
new = round_half_up(original x new_max / original_max)
```

- Use decimal arithmetic with **explicit ROUND_HALF_UP**. Do not use Python's built-in
  `round()` — it uses banker's rounding and would turn `2.5` into `2` instead of `3`.
- Always convert from the **original** score and **original** range, never from a
  previously converted value — this avoids compounding rounding error across multiple
  escalations in the same show.
- Store all three fields, never overwriting the original: `original_points`,
  `original_range_max`, `adjusted_points`. All ranking, awards, display, and export use
  `adjusted_points`; the original is the audit trail.

Verified conversion tables — use as test fixtures once implementation starts:

| Original (1–5) | → 1–10 | → 1–25 |
|---|---|---|
| 1 | 2 | 5 |
| 2 | 4 | 10 |
| 3 | 6 | 15 |
| 4 | 8 | 20 |
| 5 | 10 | 25 |

| Original (1–10) | → 1–25 |
|---|---|
| 1 | 3 |
| 2 | 5 |
| 3 | 8 |
| 4 | 10 |
| 5 | 13 |
| 6 | 15 |
| 7 | 18 |
| 8 | 20 |
| 9 | 23 |
| 10 | 25 |

## Tie-breaking

Ties are a normal, expected outcome of this system, not a rare edge case — see
DECISIONS.md for the reasoning. Resolve them in this order, and never silently:

1. Total adjusted score, descending.
2. Overall Impression, if the show has it enabled.
3. Category priority order — the organiser's ordering of active categories, compared
   one at a time.
4. Still exactly tied: Home Base asks the organiser. Never resolve silently, never
   order by database id.

**Overall Impression** is a Show Setup toggle, **off by default**: "Ask judges for an
overall impression of each car?", with one sentence noting it is used only to settle
ties. When on, the judge gives one extra score per car, in the show's current range.
It does **not** count toward the total and does **not** change Max Score.

## Awards

Two kinds:

- **Top Awards** — the organiser picks how many top-scoring cars receive an award:
  Top 10, 20, 50, 100, 150, or 200. Determined automatically using the ranking and
  tie-break cascade above. The organiser never picks these cars individually.
- **Show Awards** — seeded with seven built-ins (**Best Paint, Best Interior,
  Best Engine, Best Car, Best Truck, Best Bike, Best Rat Rod**), each independently
  on/off, renameable, reorderable, and removable. All seven default to
  **judge-chosen**.

A judge-chosen award works by **nomination**, not scoring. While judging a car, the
judge marks which awards it should be considered for — they are not scoring the award
and not picking a winner, only saying "this car belongs in the conversation." The
winner is determined by Home Base, among nominated cars only, ranked by that award's
basis:

| Award | Ranking basis |
|---|---|
| Best Paint | Paint score |
| Best Interior | Interior score |
| Best Engine | Engine score |
| Best Car | Total score |
| Best Truck | Total score |
| Best Bike | Total score |
| Best Rat Rod | Total score |

Award tie cascade: the award's ranking basis → Overall Impression, if enabled →
Total score, when it wasn't already the basis → organiser decides.

An award with **no nominations has no winner**. It is surfaced at finish-show:
"Best Rat Rod — no cars were nominated. Choose a winner or mark it as not presented."
Never silently skipped.

Adding an award asks two questions: the Award Name, and "Will judges choose this
winner?"

- **Yes** — the award appears as a nomination option on handhelds, and the organiser
  must also choose what decides the winner: Total Score, Engine, Exterior, Interior,
  Paint, or Wheels / Tires — asked plainly as "Which score should decide the winner?"
- **No** — the award never reaches a handheld. Home Base offers "Choose Winner" and the
  organiser picks any car directly. This is the right shape for Sponsor's Choice,
  Mayor's Choice, People's Choice, or a memorial award — no scoring rules needed.

A car may win any number of awards; winning one never removes it from Top Awards or
anything else. Awards can be added while a show is running. If an award changes from
judge-chosen to organiser-chosen mid-show, existing nominations stay in the database
but stop being used to determine the winner — Home Base says so plainly rather than
silently dropping them.

## Protecting completed work

Once any car has been judged, Home Base blocks certain changes — always with a plain
explanation, never a technical error:

- Manually changing the score range (moot anyway — it's automatic).
- Turning off a Judging Category that already has scores against it.
- Turning ON a Judging Category once any car in the show has been judged. This runs the
  other direction of the same rule: judging cars without a category and then switching
  it on leaves those cars permanently missing a score for it, not zero and not
  "not applicable" — there is no way to go back and score them for it later. Home Base
  says so plainly: "Cars have already been judged without Paint. Adding it now would
  leave those cars unscored in that category."

Always allowed, at any point: adding cars, adding awards, renaming awards or
categories, choosing winners, correcting participant/vehicle details, and changing
category priority order (it affects only tie-breaking, never a stored score).

Before a show can be finished, every Show Award that is **not** judge-chosen must have
a winner or be explicitly marked as not being presented — never silently skipped.
Judge-chosen awards with zero nominations are surfaced the same way.

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

### 3. Plain, human language

Every user-facing screen speaks the language of someone running a car show, not a
developer. See [LANGUAGE.md](LANGUAGE.md) — as binding as DESIGN.md. Technical
vocabulary belongs in code, database columns, and diagnostic logs, never in the normal
interface.

### 4. Resilience over cleverness

This system runs once a day, outdoors, with no IT support on site. Failures must be
**visible and recoverable, never silent**. Data already entered by a judge must survive
a crash, a battery pull, or a handheld that never comes back into range before the show
ends. When in doubt, favor the boring, debuggable, restart-safe option over a clever one.

## Hardware list

| Component | Spec |
|---|---|
| Home base | Windows laptop, runs the Python app |
| Handheld MCU | ESP32-S3-WROOM-1-N4R8 (Elecrow CrowPanel ESP32 Display 7", DIS08070H V3.0) |
| Handheld display | 800×480 RGB parallel LCD, GT911 capacitive touch (I2C) |
| Handheld camera | Arducam Mega 5MP SPI Autofocus, on a dedicated second SPI bus |
| Handheld storage | onboard microSD card (photo storage, offline queue, preferences), on its own SPI bus, separate from the camera |
| Handheld power | 3.7V 4400mAh LiPo battery |
| Router | GL.iNet GL-SFT1200 ("Opal") travel router, isolated LAN, no internet uplink |
| Handheld quantity | 3–4 concurrent judges per show |

Full pin-level detail and hardware sourcing live in `firmware/include/pins.h` and
DECISIONS.md's hardware-bring-up entries — nothing here should be re-derived or
guessed independently of those.

## Glossary

These terms get confused easily — use these definitions precisely in code, docs, and
conversation.

- **Judging Category** — one of the five built-in scoring rubric categories (Engine,
  Exterior, Interior, Paint, Wheels / Tires). Independently on/off and renameable; the
  set itself is fixed — no new categories can be added. This is what a judge actually
  scores a car against on the handheld. (Previously called "Judging Criteria" —
  renamed to match [LANGUAGE.md](LANGUAGE.md).)
- **Entry Number** — the sequential, zero-padded three-digit number Home Base
  generates for every car slot at show creation (001, 002, ...). The primary key
  humans use to refer to a car all day. It is also the photo-matching key: photo
  filenames embed it (see PROTOCOL.md). Entries start empty and are filled in as
  whoever reaches them first — judge or host — enters details. (Previously called
  "Registration Number," tied to an imported paper form; no import exists anymore —
  see "The real-world judging workflow" above.)
- **Max Score** — (active Judging Categories) × (the show's current range maximum).
  Always computed, never entered.
- **Overall Impression** — an optional, off-by-default per-car score used only to
  break ties; never counts toward the total or Max Score.
- **Top Award** — one of a fixed-count set of top-scoring cars (organiser picks the
  count: 10/20/50/100/150/200), determined automatically by ranking. No individual
  selection.
- **Show Award** — a named award (seven built-ins, plus any the organiser adds),
  either judge-chosen (won via nomination + ranking) or organiser-chosen (picked
  directly by the host — e.g. Sponsor's Choice).
- **Nomination** — a judge marking, while scoring a car, that it should be considered
  for a specific judge-chosen Show Award. Decides eligibility only; the winner is
  still determined by ranking among nominees.
- **Handheld** — one judge's ESP32-S3 device, including its display, camera, SD card,
  and battery.
- **Home Base** — the Windows laptop running the Python app; the source of truth for
  the whole show.
- **Sync Window** — a brief period during which a handheld has WiFi radio on and is
  actively calling `POST /api/v1/sync` (see PROTOCOL.md). Outside a sync window, a
  handheld's WiFi radio is off to save battery and it operates entirely from its local
  queue and cached configuration/roster.
- **Configuration Revision / Show Data Revision** — the two monotonically increasing
  integers Home Base uses to tell a handheld what it hasn't seen yet (see
  PROTOCOL.md). Internal sync bookkeeping only — never shown to a user; see
  [LANGUAGE.md](LANGUAGE.md).

## Related documents

- [DESIGN.md](DESIGN.md) — visual design system for both platforms.
- [LANGUAGE.md](LANGUAGE.md) — interface language standard; as binding as DESIGN.md.
- [PROTOCOL.md](PROTOCOL.md) — the network and data contract between handheld and home base.
- [DECISIONS.md](DECISIONS.md) — running log of architectural decisions and open questions.
- [README.md](README.md) — setup, running, and the offline prep checklist.
