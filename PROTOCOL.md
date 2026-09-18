# PROTOCOL.md — Handheld ↔ Home Base Contract

This is the full network and data contract between a handheld and home base. It should
be precise enough that the PC app and the firmware can be built independently, by
different sessions or people, and still interoperate correctly. Read
[CONTEXT.md](CONTEXT.md) first for the vocabulary used here (entry number, sync
window, judging category, etc.) and for the scoring/awards business rules referenced
below — this document covers wire format, not why the rules exist.

## Transport

- **Base URL:** `http://<homebase-ip>:8000/api/v1/`
- **Transport:** REST + JSON over plain HTTP, on the isolated show LAN provided by the
  GL.iNet router.
- **No TLS. No auth.** This is a deliberate choice for a closed, single-day event
  network with no internet uplink and no adversary model beyond "other people at a car
  show." If this assumption ever changes (e.g. the network stops being fully isolated,
  or the router's admin interface becomes reachable by attendees), the addition would
  be a shared pre-shared key sent as a request header (e.g. `X-Show-Key: <key>`),
  checked by home base on every endpoint, distributed to handhelds at provisioning
  time. Not implemented now — noted here so it isn't forgotten if the trust model
  changes.

## Endpoints

### `GET /api/v1/health`

No parameters. Returns:

```jsonc
{
  "server_time": "2026-08-30T14:32:10Z",
  "show_name": "Top Spot Autumn Cruise-In"
}
```

A cheap reachability check — used by handhelds and dev tooling without exchanging any
show data.

### `POST /api/v1/sync`

The single sync endpoint. Pushes whatever scored cars are queued on the handheld and
pulls back everything the handheld hasn't seen yet, in one round trip.

Request:

```jsonc
{
  "handheld_id": "hh-2",
  "show_id": 42,                 // cached active Show.id; 0 when no valid show is cached
  "config_revision": 4,          // last configuration_revision this handheld applied
  "data_revision": 812,          // last show_data_revision this handheld applied
  "known_car_count": 310,        // number of cars in the handheld's cached roster
  "battery_pct": 62,
  "submissions": [
    {
      "entry_number": "142",
      "judge_name": "R. Alvarez",              // optional
      "closed_at_uptime_ms": 1893442,          // ms since handheld boot — see below
      "participant": "R. Alvarez",
      "year": "1969",
      "make": "Chevrolet",
      "model": "Camaro",
      "vehicle_type": "Car",
      "make_manually_entered": false,
      "model_manually_entered": true,
      "score_range_max": 10,                   // REQUIRED — see below
      "scores": [
        { "category_id": 3, "points": 8 }
      ],
      "overall_impression": null,               // null when the show has it disabled
      "nominations": [12, 15]                   // award ids
    }
  ]
}
```

**An empty `submissions` array is valid and expected.** A handheld with nothing queued
still calls this endpoint on its periodic wake purely to refresh `server_time`,
configuration, roster state, and the show-wide summary.

Response:

```jsonc
{
  "server_time": "2026-08-30T14:32:11Z",       // handheld sets its clock from this
  "config_revision": 4,
  "data_revision": 814,
  "sync_mode": "DELTA",                       // "FULL" replaces the entire cached roster
  "configuration": { /* ... */ } | null,       // show changed or configuration revision is stale
  "cars": [ /* ... */ ],                       // complete roster for FULL; changed entries for DELTA
  "vehicle_additions": [ /* ... */ ],          // approved vehicle names — see note below
  "results": [
    { "entry_number": "142", "status": "accepted", "message": "" }
    // status is one of: "accepted" | "already_recorded" | "flagged_duplicate" | "error"
  ],
  "summary": {
    "total_cars": 310,
    "judged": 142,
    "unjudged": 165,
    "flagged_conflict": 3
  }
}
```

**`summary` is always computed from the full current roster, never scoped to the
delta.** It's a show-wide progress snapshot that every handheld displays on its status
bar, regardless of whether the rest of the response is a delta or a full pull.

#### Revision counters, not timestamps

Every sync request sends the cached active `show_id` as a nonnegative integer,
or `0` when no valid show is cached. For older clients that omit it, Home Base
defaults it to `0`, forcing an authoritative refresh.

Home Base computes `show_changed = request.show_id != active_show.id`.
Configuration is sent when `show_changed` or the requested `config_revision`
is lower than the active show's configuration revision. Sync mode is `FULL`
when `show_changed`, `data_revision <= 0`, or `known_car_count` differs from
the authoritative roster count. FULL always returns the complete active-show
roster, ignoring revisions from another show. Otherwise mode is `DELTA` and
only cars changed since the requested data revision are returned.

The handheld adopts and persists received configuration, including its show ID.
A FULL roster replaces the cached car list, including when the list is empty;
old-show cars must never be merged into the new show. Local pending-status
overlays apply only to records belonging to that roster's show. Submission
acknowledgements and retry behavior are unchanged.

Home Base maintains two monotonically increasing integers per show:

- `configuration_revision` — bumped on any Show Setup change: categories, awards,
  award settings, score range escalation, category priority order.
- `show_data_revision` — bumped on any change to cars: entries added, participant or
  vehicle details entered or corrected.

Every car (entry) row carries the `show_data_revision` at which it last changed, so
the server returns only entries a given handheld has not seen. Each handheld stores
the last `config_revision` and `data_revision` it successfully applied and sends both
on every call. Handhelds have no battery-backed clock and take their time from Home
Base — comparing integers is more reliable across devices than comparing timestamps
that could be wrong before the first sync of the day.

`configuration`, when present, carries whatever a handheld needs to render judging
correctly: the active Judging Categories (with names and priority order), the show's
current score range, the show's Max Score, whether Overall Impression is enabled, and
the judge-chosen Show Awards available for nomination:

```jsonc
{
  "show_id": 42,                            // Show.id — stable across a rename, unlike show_name;
                                             // see DECISIONS.md's F4 entry (vehicle_recents.h scopes
                                             // "recently used this show" against this, not the name)
  "show_name": "Top Spot Autumn Cruise-In",
  "event_date": "2026-09-12",                // Show.event_date, ISO YYYY-MM-DD — used offline as the
                                             // Vehicle Details Year field's upper bound (event year + 1),
                                             // no RTC/NTP/network needed at judging time — see DECISIONS.md
  "score_range_max": 10,
  "max_score": 40,                          // active categories x score_range_max —
                                             // computed by Home Base, never by the
                                             // handheld, so the two can never disagree
  "overall_impression_enabled": false,
  "categories": [
    { "id": 1, "name": "Engine", "sort_order": 0 },
    { "id": 2, "name": "Exterior", "sort_order": 1 },
    { "id": 3, "name": "Interior", "sort_order": 2 },
    { "id": 4, "name": "Paint", "sort_order": 3 }
    // a 5th category, Wheels / Tires, is inactive for this show and so
    // does not appear here — inactive categories are never sent
  ],
  "judge_chosen_awards": [
    { "id": 12, "name": "Best Paint" },
    { "id": 15, "name": "Best Engine" }
  ]
}
```

#### score_range_max is mandatory on every submission

A handheld can hold finished cars while offline and then sync after the show's range
has escalated (see CONTEXT.md). Home Base has no other way to tell a 4-out-of-5 from a
4-out-of-10. On receipt, if `score_range_max` differs from the show's current range,
Home Base converts before storing (per CONTEXT.md's conversion formula) and keeps the
values exactly as sent for the audit trail.

#### Score validation

Every active Judging Category must have an entry in `scores`, and each score's
`points` must fall within `1..score_range_max` — the submission's own
`score_range_max` as sent, not the show's current range. Either problem
rejects the WHOLE item with `status: "error"` and a message naming the
category and the problem (e.g. "Paint has not been scored. Choose a Paint
score before continuing." or "Paint score of 12 is outside the allowed range
(1-5)."). Never a partial score set, and never a silent 0 for an unscored
category — see CONTEXT.md: there is no zero and no "not applicable."

#### Entry details: fill or correct

`participant`/`year`/`make`/`model`/`vehicle_type` on a submission apply to
its entry this way: a blank field on the entry is always filled from the
submission. A field that already holds a value is overwritten only when the
submission's value is both non-empty and different from what's stored —
that's a judge correcting a wrong pre-fill, not a conflict — and it's applied
directly. Corrections are logged internally (diagnostic log, not a
user-facing error) and never rejected or flagged back to the handheld.

#### Idempotency

A handheld that loses the connection before receiving an acknowledgement **will
retry**. A submission with the same `entry_number` + `handheld_id` +
`closed_at_uptime_ms` is a retry: Home Base returns `"already_recorded"` and changes
nothing. **This is not a duplicate conflict** — treating it as one would generate
false conflicts all show long.

A second, genuinely different submission for the same `entry_number` (a different
`closed_at_uptime_ms` — e.g. two judges reached the same car) is a real conflict:
marked `"flagged_duplicate"`, and the car's status becomes `flagged_conflict`. The
original accepted submission is never overwritten; both are retained and the host
resolves the conflict manually in Home Base.

#### closed_at_uptime_ms

Before its first successful sync of the day, a handheld has no valid wall-clock time,
so it reports milliseconds since its own boot. Home Base converts that to a real
timestamp using its own clock at receipt.

#### vehicle_additions (deferred)

`make_manually_entered` / `model_manually_entered` on a submission, and
`vehicle_additions` in the response, point at a controlled make/model vocabulary —
approved vehicle names a handheld can offer instead of free text. The full semantics
(what makes an addition "approved," how it propagates back out to other handhelds)
are defined by SPEC-B, in the master build guide (the three-layer vehicle database,
review queue, and vehicle_additions spec).

What exists today: whenever either manually-entered flag is true, Home Base records a
candidate sighting for that make/model pairing (`services/vehicle_candidates.py`) —
raw material for the review queue. `vehicle_additions` in the response is still always
`[]`: the review/approval workflow that decides what counts as "approved" (and the
revision-style delta logic to only send what's new since the handheld's last update)
is HB5's job, not built yet — see DECISIONS.md.

### `POST /api/v1/photos/upload`

`multipart/form-data` fields: `entry_number`, `photo_type`, `file`.

This is the **WiFi fallback path** for photo transfer, used only when the handheld's
microSD card can't be read directly on Home Base's computer post-judging. There is no
USB transfer path at all — the handheld's USB-C port is a CH340C serial programming
bridge, and the ESP32-S3's native USB pins are already committed to the touchscreen
(see firmware/include/pins.h), so it can never present itself as a drive. This endpoint
shares the same server-side ingest logic as the SD card path — both end up writing the
same file naming convention (below) into the same photo store, so home base doesn't
need to know or care which transport a given photo arrived through.

## Handheld sync trigger model

**Two triggers, one routine.**

- **(a) Event-driven:** a judge closes out a car → attempt sync immediately, ignoring
  backoff.
- **(b) Periodic:** a timer fires independently (default 3 minutes, configurable) to
  check whether the router has come back into range.

The routine, run on either trigger:

1. **Wake. Do a WiFi scan for the configured SSID only — do not associate yet.** This
   is the cheap step, and the one that runs most often when the handheld is out of
   range.
2. **SSID not found → sleep immediately.** Apply backoff to the *periodic* timer only
   (double the interval, cap at 15 minutes). Event-driven triggers always scan at base
   cost regardless of the periodic timer's current backoff state — a judge closing a
   car should never be penalized by how long the handheld has been out of range.
3. **SSID found → associate, get an IP, then `POST /api/v1/sync`** with whatever is
   queued (possibly empty). Apply the response's `configuration`, `cars`,
   `vehicle_additions`, `summary`, and `server_time` locally. Reset the periodic
   backoff to the base interval. Turn WiFi off.
4. **If associate or the POST fails after a successful scan** (the AP is up but home
   base is down or unreachable), treat it as a miss for backoff purposes and **leave
   the submission queue intact** — nothing queued is ever dropped on a failed sync
   attempt.

## Photo naming convention

This is how photos find their car — it's the only linkage between a photo file and a
car record, so it must be followed exactly by firmware and by home base's ingest logic
on both transports (SD card and WiFi upload).

```
{entry_number}_car.jpg
{entry_number}_sheet.jpg
```

- `_car.jpg` — photo of the car itself.
- `_sheet.jpg` — photo of the paper judge sheet for that car.

Stored on the handheld's microSD card during judging; matched by home base on ingest,
regardless of which transport (SD card or WiFi) delivered the file.
