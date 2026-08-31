# Router Setup — GL.iNet GL-SFT1200 (Opal)

Do this once per show, before handhelds are handed to judges. See
[show-day-runbook.md](show-day-runbook.md) for the full day-of sequence this
slots into — this document is just the router.

## The values everything else depends on

Firmware settings (`firmware/src/storage/settings.h`) and this document are
built to agree on these exact numbers. If you ever change one, change the
other — they're a single source of truth split across two files, not two
independent choices.

| What | Value | Why |
|---|---|---|
| Router's own LAN address | `192.168.8.1` | The GL-SFT1200's factory default gateway. Don't change it — nothing gains from moving it, and it's what the router's admin page is always reachable at. |
| **Home Base's reserved address** | **`192.168.8.2`** | Fixed by DHCP reservation (below), not typed in by hand on every handheld. This is the handheld firmware's compiled-in default (`Settings::homeBaseAddress`) — a freshly flashed handheld needs zero configuration to find Home Base as long as this reservation exists. |
| Home Base's port | `8000` | What `uvicorn` is started on — see the runbook's "Starting Home Base" step. Matches PROTOCOL.md's base URL. |
| Show Wi-Fi network name (SSID) | *your choice, e.g.* `TopSpot-Show` | Set fresh per show (below) — there is no compiled-in default; `wifiSsid` ships blank on purpose so a real password is never sitting in firmware source. |
| Show Wi-Fi password | *your choice* | Same reasoning — set it during router setup, then write it down (step 2) for provisioning handhelds. |

## 1. Connect to the router's admin page

1. Power on the GL-SFT1200. Give it a minute to boot.
2. On a laptop or phone, connect to its factory Wi-Fi network (printed on a
   sticker on the underside of the router) and open `http://192.168.8.1` in a
   browser.
3. If this is the router's first-ever setup, it will ask you to set an admin
   password — do that now and write it down somewhere separate from the show
   Wi-Fi password (step 2). If it's already been through first-time setup,
   log in with that password.

## 2. Create the show Wi-Fi network

1. In the admin page, find the Wi-Fi / Wireless settings (on GL.iNet's
   interface this is usually the first thing on the home screen — "Wi-Fi
   name" / SSID and password fields).
2. Set the **SSID** to something short and easy to type on a handheld's
   on-screen keyboard — no spaces makes this faster (e.g. `TopSpot-Show`).
   Avoid the factory default name; a room full of phones scanning for
   networks at a car show makes a generic default name ambiguous to pick
   out.
3. Set a **password** (WPA2, 8+ characters). Write both the SSID and password
   down on a card that travels with the laptop — every handheld's Settings
   screen needs these typed in exactly (**Show Wi-Fi Name** / **Show Wi-Fi
   Password** fields — see the runbook).
4. Save. The router will briefly restart its Wi-Fi radio.

## 3. Reserve a fixed address for the Home Base laptop (DHCP reservation)

This is the step that makes every handheld find Home Base with **no manual
IP entry** — do it before connecting any handhelds.

1. Connect the Home Base laptop to the show Wi-Fi network you just created.
2. On the laptop, find its Wi-Fi MAC address: open a terminal and run
   `ipconfig /all`, find the adapter connected to the show network (usually
   "Wireless LAN adapter Wi-Fi"), and note its **Physical Address**
   (format `XX-XX-XX-XX-XX-XX`).
3. Back in the router admin page, find the LAN / DHCP settings (on GL.iNet's
   interface, usually under Network → LAN, with a "DHCP reservation" or
   "Static IP" list, sometimes shown as a per-client option in the connected
   Clients list once the laptop has joined).
4. Add a reservation: the laptop's MAC address from step 2 → IP address
   **`192.168.8.2`**. Save.
5. The GL-SFT1200's default DHCP pool typically starts well above `.2`
   (commonly `.100` and up) — confirm on this specific unit's DHCP settings
   page that `.2` isn't inside the dynamic pool range before saving, so a
   handheld or phone can never be handed that address by accident.
6. Reconnect the laptop's Wi-Fi (or reboot it) so it actually picks up the
   reservation, then confirm with `ipconfig` that its IPv4 address is now
   `192.168.8.2`.

## 4. Confirm handhelds can reach it

1. Start Home Base on the laptop (see the runbook — `--host 0.0.0.0 --port
   8000` matters here too, or the reservation alone won't help).
2. On each handheld's Settings screen, set **Show Wi-Fi Name** / **Show
   Wi-Fi Password** to what you wrote down in step 2. Leave **Home Base
   Address** at its default (`192.168.8.2:8000`) — it should already match,
   since that's exactly the address you just reserved.
3. Wait for the handheld's status bar to read **Up to Date** (green).
4. On the laptop, open Home Base's **Handhelds** page and confirm the device
   shows up there too, also **Up to Date**. Do this for every handheld before
   sending judges out — it's much easier to fix a typo'd Wi-Fi password here
   than to chase it down mid-show.

## 5. Turn off what's not needed

The GL-SFT1200 ships with several features this show never uses. Leaving
them on doesn't usually break anything, but it's one less thing to explain
if something looks unfamiliar on the admin page mid-show, and some of them
actively work against an intentionally-offline network:

- **WAN / internet uplink** — this router provides **no internet** by design
  (CONTEXT.md). If a WAN connection type is configured (cellular, a second
  Wi-Fi network as uplink, an Ethernet WAN port), disconnect or disable it.
  An uplink that keeps trying to reach the internet and failing is just
  wasted radio/CPU time on a small travel router.
- **Repeater / Extender mode** — make sure the router is running as its own
  access point, not repeating another network. Confirm it's in the normal
  AP/router operating mode, not "Repeater."
- **Guest Wi-Fi** — if enabled, turn it off. One network, the show network,
  is simpler to reason about and there's no reason for a second SSID here.
- **VPN client** — GL.iNet routers ship with WireGuard/OpenVPN client
  support. Confirm no VPN profile is active; it would try to reach a remote
  server that doesn't exist on this network anyway, for no benefit.
- **Automatic firmware/update checks** — irrelevant with no internet uplink,
  but turning it off avoids a retry loop in the background. Not required,
  just tidy.
- **Bluetooth**, if the unit has it and it's on — not used by anything here.

None of this is required for the system to work — CONTEXT.md's isolated-LAN
design tolerates all of it being left on. It's a cleanliness/predictability
step, not a functional one.

## 6. Placement for field coverage

- The GL-SFT1200 is a small travel router — its range is closer to a home
  Wi-Fi router than a venue-grade access point. Judges are **expected** to
  regularly walk out of range (CONTEXT.md, PROTOCOL.md's whole sync design is
  built around this) — perfect coverage of the entire field is not the goal,
  and isn't achievable with one small router anyway.
- Place it centrally to the area judges will actually cover, elevated if
  possible (on a table or shelf inside the registration tent/canopy, not on
  the ground) — height and a clear line of sight matter more than exact
  distance for a 2.4/5GHz radio like this one.
- Keep it away from large metal surfaces (a show trailer wall, a stack of
  folding tables) and other 2.4GHz sources (a nearby PA system, other
  vendors' routers) where practical.
- Keep the Home Base laptop within the same practical range as the router —
  it needs a stable connection just as much as any handheld, and it's the
  one device that can't just "sync when it wanders back into range."
- Power: run the router from wall power via an extension cord if the
  registration area has it; otherwise a USB battery bank rated for the
  GL-SFT1200's draw works for a full show day — confirm its runtime ahead of
  time, not on the morning of the show.

## 7. Recovery if the router is power-cycled mid-show

A router losing power (someone unplugs it, a breaker trips, a battery bank
dies and gets swapped) is recoverable without reconfiguring anything:

1. **The DHCP reservation and Wi-Fi settings survive a power cycle** — they're
   saved to the router's flash storage, not lost when power drops. Only a
   full factory reset would clear them, and a factory reset is never part of
   recovering from a power loss.
2. Power the router back on. Give it a minute or two to fully boot and bring
   its Wi-Fi radio back up.
3. **Home Base doesn't need to be restarted** — it keeps running on the
   laptop the whole time; it just has no handhelds to talk to until the
   network comes back. Leave it running.
4. **Handhelds recover on their own** — PROTOCOL.md's sync trigger model
   already treats "router unreachable" as the same ordinary case as "judge
   walked out of range": each handheld's periodic timer keeps retrying with
   backoff (capped at 15 minutes) until the SSID is visible again, and
   reconnects the moment it is. No judge action is needed — nothing queued on
   a handheld is ever lost while this happens (CONTEXT.md's resilience
   principle).
5. Once the router is back up, spot check the **Handhelds** page on Home
   Base — everything should return to **Up to Date** within a few minutes as
   each device's next sync attempt lands. If a specific handheld doesn't
   recover after several minutes near the router, treat it as "a handheld
   will not connect" in the runbook's troubleshooting section.
6. The one case that DOES need a repeat of this document: swapping in a
   **different, unconfigured router unit** (a hardware failure, not just a
   power blip) — that's a factory-default device with none of the above set
   up, and steps 1–4 need to be redone against it, including registering a
   fresh DHCP reservation and reconfiguring every handheld's Show Wi-Fi
   fields if the SSID/password also changed.

## See also

- [show-day-runbook.md](show-day-runbook.md) — the full day-of sequence,
  including starting Home Base and troubleshooting a handheld that won't
  connect.
- [../README.md](../README.md) — repository overview and offline prep
  checklist.
- [../PROTOCOL.md](../PROTOCOL.md) — the sync trigger model referenced in
  section 7 above.
