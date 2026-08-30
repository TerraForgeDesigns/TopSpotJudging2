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
