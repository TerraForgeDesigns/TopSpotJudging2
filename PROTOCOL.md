# PROTOCOL.md — Handheld ↔ Home Base Contract

This is the full network and data contract between a handheld and home base. It should
be precise enough that the PC app and the firmware can be built independently, by
different sessions or people, and still interoperate correctly. Read
[CONTEXT.md](CONTEXT.md) first for the vocabulary used here (registration number,
sync window, etc).

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

### `GET /sync/roster`

Query params: `since=<ISO8601>` (optional), `handheld_id=<id>` (required).

Pulls the current roster, criteria, and classes. If `since` is omitted, returns the
full roster (used on first sync of the day). If `since` is present, returns only
cars/classes/criteria created or updated after that timestamp.

Response:

```jsonc
{
  "server_time": "2026-08-29T14:32:10Z",   // handhelds set their clock from this
  "cars": [ /* cars created/updated since `since`, or full roster if `since` omitted */ ],
  "classes": [ /* car classes */ ],
  "criteria": [ /* judging criteria */ ],
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

### `POST /sync/submissions`

Pushes whatever scored cars are queued on the handheld, and pulls back a fresh delta
in the same round trip.

Request:

```jsonc
{
  "handheld_id": "hh-2",
  "since": "2026-08-29T13:05:00Z",   // optional — controls the roster_delta in the response
  "submissions": [
    {
      "registration_number": "0142",
      "judge_name": "R. Alvarez",    // optional
      "closed_at": "2026-08-29T14:31:52Z",
      "scores": [
        { "criteria_name": "Paint", "points": 18 },
        { "criteria_name": "Engine", "points": 22 }
      ]
    }
  ]
}
```

**An empty `submissions` array is valid and expected.** A handheld with nothing queued
still calls this endpoint on its periodic wake purely to refresh `server_time`, roster
state, and the show-wide summary.

Response:

```jsonc
{
  "server_time": "2026-08-29T14:32:11Z",
  "results": [
    { "registration_number": "0142", "status": "accepted", "message": "" }
    // status is one of: "accepted" | "flagged_duplicate" | "error"
  ],
  "roster_delta": { /* same shape as GET /sync/roster's response */ },
  "summary": { /* same shape as above */ }
}
```

### `POST /photos/upload`

`multipart/form-data` fields: `registration_number`, `photo_type`, `file`.

This is the **WiFi fallback path** for photo transfer, used only when USB transfer
isn't available post-judging. It shares the same server-side ingest logic as the USB
path — both end up writing the same file naming convention (below) into the same photo
store, so home base doesn't need to know or care which transport a given photo arrived
through.

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
3. **SSID found → associate, get an IP, then `POST /sync/submissions`** with whatever
   is queued (possibly empty). Apply the response's `roster_delta`, `summary`, and
   `server_time` locally. Reset the periodic backoff to the base interval. Turn WiFi
   off.
4. **If associate or the POST fails after a successful scan** (the AP is up but home
   base is down or unreachable), treat it as a miss for backoff purposes and **leave
   the submission queue intact** — nothing queued is ever dropped on a failed sync
   attempt.

## Duplicate handling

If a car already has an accepted submission, a second submission for the same
registration number is marked `flagged_duplicate`, and the car's status becomes
`flagged_conflict`. **The original accepted submission is never overwritten.** Both
submissions are retained; the host resolves the conflict manually in the home base app
(e.g. two judges scored the same car, or a handheld retried a submission that actually
went through the first time).

## Photo naming convention

This is how photos find their car — it's the only linkage between a photo file and a
car record, so it must be followed exactly by firmware and by home base's ingest logic
on both transports (USB and WiFi upload).

```
{registration_number}_car.jpg
{registration_number}_sheet.jpg
```

- `_car.jpg` — photo of the car itself.
- `_sheet.jpg` — photo of the paper judge sheet for that car.

Stored on the handheld's SD card during judging; matched by home base on ingest,
regardless of which transport (USB or WiFi) delivered the file.
