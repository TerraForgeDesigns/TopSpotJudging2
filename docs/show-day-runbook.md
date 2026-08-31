# Show Day Runbook

For whoever is running Top Spot Judging on site — not necessarily the person who
built it. Short steps, in order. If something breaks, skip to **When something
goes wrong** and find the matching heading.

## 1. Before you leave

- [ ] Laptop charged, and its charger packed. Home Base runs the whole show — if it
      dies, judging stops.
- [ ] All 3–4 handhelds charged overnight.
- [ ] The GL.iNet router charged/packed, plus its power adapter.
- [ ] Confirm Home Base already has the show set up (Show Details, Number of Cars,
      Judging Setup, Awards) — do this the night before, not in the parking lot.
- [ ] If this is a new laptop or a fresh install, confirm the one-time offline setup
      in [README.md](../README.md#offline-prep-checklist) was actually done —
      Python dependencies installed and the font files vendored. Nothing at the show
      itself needs the internet; everything it needs must already be on the laptop.

## 2. Setting up the router

Full instructions — creating the show Wi-Fi network, reserving Home Base's fixed
address, and what to turn off — live in
[router-setup.md](router-setup.md). Do that once, ahead of time (it only needs
redoing if you swap in a different, unconfigured router unit). On the day itself:

1. Power on the GL.iNet GL-SFT1200. Give it a minute to boot.
2. The show Wi-Fi network name (SSID) and password are whatever was set during
   router setup — written on a card kept with the laptop, not the router's factory
   default. Every handheld and the laptop connect to this SAME network.
3. This router is **not** connected to the internet, and that's correct — it only
   creates a local network between the laptop and the handhelds. Don't waste time
   trying to get it "online."

## 3. Starting Home Base

1. On the laptop, connect to the show WiFi network the router just created (same
   network as the handhelds, not a different one).
2. Open a terminal in the `homebase` folder and run:

   ```
   .venv\Scripts\python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
   ```

   `--host 0.0.0.0` matters — without it, only the laptop itself can reach Home
   Base, and every handheld will show **Not Connected** all day.
3. On the laptop's own browser, go to `http://localhost:8000/` and confirm the
   right show is showing at the top of the sidebar (its name, not "Switch show").
   If it's the wrong show, click **Switch show** and pick the right one.
4. As long as [router-setup.md](router-setup.md)'s DHCP reservation is in place,
   the laptop always gets `192.168.8.2` on the show network — there's nothing to
   look up here. If `ipconfig` shows something else, the reservation either isn't
   set up yet or didn't take; fix it in the router's admin page before continuing
   (a handheld can't find Home Base at the wrong address).

## 4. Preparing and checking handhelds

For each handheld, before judges start walking the field:

1. Power it on. On its **Settings** screen, confirm **Show Wi-Fi Name** and
   **Show Wi-Fi Password** match the router. Leave **Home Base Address** at its
   default (`192.168.8.2:8000`) — it should already match, since that's exactly
   the fixed address [router-setup.md](router-setup.md) reserves. Only change it
   if step 3.4 above showed the laptop landed on a different address.
2. Wait for the status bar to show **Up to Date** (green) — that's a real,
   successful connection to Home Base, not just to the WiFi network.
3. On the laptop, open the **Handhelds** page and confirm this device appears and
   also shows **Up to Date**. If it's missing entirely, it has never successfully
   reached Home Base yet — see below.
4. Check the battery indicator. Anything already showing a low-battery warning
   should be charged before it goes out with a judge.
5. Hand each judge their assigned handheld and confirm they know their entry-number
   range or area of the field, per the normal "get to them as you can" approach —
   there's no assigned route.

## 5. When something goes wrong

### A handheld will not connect

1. Confirm Home Base is actually running (step 3) — a closed terminal window means
   Home Base is off, and every handheld will read **Not Connected**.
2. On the handheld's Settings screen, double check **Show Wi-Fi Name**,
   **Show Wi-Fi Password**, and **Home Base Address** character-for-character —
   this is the single most common cause.
3. Still stuck? A handheld regularly losing and regaining WiFi range is completely
   normal — CONTEXT.md calls this the expected operating condition, not an error.
   Give it a few minutes near the router before assuming something is actually
   wrong. A handheld reads **Updating** (not **Not Connected**) for up to 15
   minutes without it being a real problem.
4. If it's still **Not Connected** after standing right next to the router: power
   cycle the handheld. If that doesn't fix it, the judge can keep scoring offline —
   nothing is lost (see CONTEXT.md's resilience principle) — and it will catch up
   the next time it reconnects.

### Photos will not import

1. On the **Photos** page, click **Scan now** rather than waiting for the
   automatic scan.
2. Confirm the memory card is actually a card Windows recognizes as a removable
   drive — try a different USB card reader if you have one.
3. If a file genuinely can't be read, Home Base tells you which one and why on the
   Photos page — it never fails the whole import over one bad file. Everything
   else on the card still imports.
4. Photos from an entry number Home Base doesn't recognize aren't lost — they land
   under **Unmatched** on the Photos page. Assign them to the right car by hand
   there.

### A car was judged twice

This is expected, not a bug — two judges reaching the same car is normal at a
busy show. Home Base never guesses which score to keep.

1. Go to **Conflicts**. The car will be listed with both submissions side by side.
2. Compare them against the paper judge sheets (or the photos, once imported) and
   choose which one to keep — or enter a corrected score set by hand if neither
   matches what's actually on the paper.

### A wrong entry number was used

1. Go to **Cars**, find the entry, and open it to edit.
2. Participant, Year, Make, Model, and Vehicle Type can all be corrected directly
   there at any time — this is always allowed, even after judging (CONTEXT.md).
3. If the SCORE itself was recorded against the wrong car entirely (not just a
   wrong detail), that's the same situation as "judged twice" above — resolve it
   through **Conflicts**, or enter the correct score by hand there if the car
   currently has no valid score at all.

### A handheld died with work unsent

1. Check the **Handhelds** page — if it last showed **Up to Date** recently, most
   of its work already made it to Home Base. Only what was scored AFTER its last
   successful update is at risk.
2. If the handheld can be revived (new battery, etc.), power it back on near the
   router — anything still in its local queue sends automatically the moment it
   reconnects. Nothing queued is ever silently dropped by the handheld itself.
3. If the handheld truly cannot be revived before the show ends, any car it scored
   but never sent is genuinely lost and needs to be judged again by another
   handheld — there is no way to recover it from Home Base, since Home Base never
   received it. Check **Cars** for entries still showing **Unjudged** and get them
   covered.

### The vehicle list is missing a car

This is expected for anything unusual (a rare classic, a kit car, a motorcycle
Home Base's built-in list doesn't know) — it is never a reason judging can't
continue.

1. On the handheld, use **Other / Enter Manually** on the Make or Model screen and
   type it in directly. Judging proceeds exactly the same either way.
2. Home Base quietly keeps a record of every manually-typed make/model — useful
   later for expanding the built-in list, but nothing the judge needs to think
   about in the moment.
