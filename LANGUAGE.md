# LANGUAGE.md — Interface Language Standard

Read this alongside [DESIGN.md](DESIGN.md) — it is **equally binding**. DESIGN.md
governs what the interface looks like; this document governs what it says. Read
[CONTEXT.md](CONTEXT.md) first for the vocabulary this document translates.

## Principle

Top Spot Judging is operated by people running a car show, not developers. A
first-time user must be able to run it with nobody explaining what the buttons mean.
Technical vocabulary lives freely in code, database columns, and diagnostic logs — it
never appears in the normal interface, on a Home Base screen, or on a handheld.

## Never use → always use

The left column must never appear in normal user-facing text, on either platform.

| Never say | Say instead |
|---|---|
| Criterion / Criteria | Judging Category / Judging Categories |
| Configuration | Show Setup |
| Configuration Revision | never displayed |
| Show Data Revision | never displayed |
| Synchronization / Sync | Updating / Up to Date |
| Record / Judging Record | Score / Judging Results |
| Submission | Submit Scores |
| Entity | Car / Entry |
| Persist / Persistence | Save / Saved |
| Endpoint | never displayed |
| Payload | never displayed |
| Authentication | Sign In |
| Initialize | Start / Starting |
| Validation Error | explain the actual problem |
| Network Unavailable | Home Base Not Connected |
| Hardware Peripheral | Camera / SD Card / Device |
| Maximum Possible Score | Max Score |
| Manual Award Assignment | Choose Winner |
| Configuration Update | Show Setup Updated |

## Preferred vocabulary

Show, Cars, Entry Number, Participant, Judging, Judging Category, Score, Max Score,
Awards, Choose Winner, Handheld, Photos, Connected, Not Connected, Updating,
Up to Date, Add Cars, Edit Show, Year, Make, Model, Other, Search, Done, Back, Cancel.

## Recurring state phrasing — identical on both devices

Home Base and the handheld both describe the same handful of states constantly — a
handheld's connection, a car's judging status, whether something saved. The words below
are the only ones either device may use for these states. Neither platform may invent a
synonym for one of these — a judge who learns what "Updating" means on the handheld
must see the identical word for the identical state on Home Base's screen, and back.
Sourced from Home Base's UI as already built (`handhelds.html`, `partials/
dashboard_live.html`, `cars/list.html`) — this is the reference the handheld's own
judging-flow screens must match when they're built; nothing in the firmware
overrides it.

**A handheld's connection, as Home Base shows it** (a colored pill; thresholds from
`services/dashboard.py`):

| Word | Color | Meaning |
|---|---|---|
| Up to Date | green | Heard from this handheld in the last 3 minutes. |
| Updating | blue | Last heard from 3–15 minutes ago — normal while a judge is out of Wi-Fi range, not an error. |
| Not Connected | red | Not heard from in over 15 minutes, or never. |

**Home Base's connection, as the handheld shows it** — the same three words, describing
the same link from the other side. When a full sentence is needed instead of a status
word, the established phrasing is already in the Never/Always table above: "Home Base
Not Connected," or spelled out per the error-message standard, "Home Base could not be
reached. Check that Home Base is running and this device is connected to the show
Wi-Fi."

**A car's judging status:**

| Word | Color | Meaning |
|---|---|---|
| Judged | green | Has one accepted set of scores. |
| Unjudged | gray | No accepted score yet — never shown as "0" or left blank; see CONTEXT.md's scoring section. |
| Flagged Conflict | red | Two handhelds both produced a score for this car. The host chooses which to keep. |

**Saving:** "Save" is the action, "Saved" is the confirmation once it's done — never
"Persist," "Persisted," "Synced," or "Submitted." A handheld that writes a score to its
own storage before Home Base has ever seen it should say "Saved" the moment that local
write completes, not only once Home Base has received it — the judge needs to know the
entry survived even while still out of Wi-Fi range.

**Photo coverage is Home Base only.** Home Base additionally shows "Both," "1 of 2," and
"Missing" for a car's photo coverage (Cars section) — there's no handheld equivalent,
since a handheld has no way to know what the other device's camera captured. Don't
invent a matching handheld phrase for this one.

## Diagnostic-only vocabulary

The following must never appear on a handheld outside a hidden diagnostic screen: API,
HTTP, database, JSON, SPI, revision, payload, endpoint, peripheral, configuration,
validation, dataset, lookup table, index, schema, query, cache, partition,
memory-mapped, or any synchronization-protocol vocabulary. If a debug/diagnostic
screen needs this language, it must be clearly separated from — and unreachable
through — the normal judging flow.

## Error message standard

Every user-facing error states two things: what happened, and what to do about it.
Never just what failed technically.

| Don't | Do |
|---|---|
| "Failed to fetch." | "Home Base could not be reached. Check that Home Base is running and this device is connected to the show Wi-Fi." |
| "Validation failed: required criterion missing." | "Engine has not been scored. Choose an Engine score before continuing." |
| "SD initialization failed." | "Photos cannot be saved. Storage is not available." |

Technical detail (stack traces, status codes, exact field names) still goes to
diagnostic logs — never removed, just never shown to the person running the show.

## Applying this document

When writing any new user-facing string:

1. Check the mapping table above before using a term that sounds technical — if it's
   on the left, don't ship it.
2. Write the error-message pair (what happened / what to do) before writing any other
   copy for that screen.
3. If in doubt, read it back as if explaining it to someone at the show who has never
   seen the software before. If it needs a follow-up question, rewrite it.
