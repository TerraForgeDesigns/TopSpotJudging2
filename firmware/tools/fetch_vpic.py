"""One-time dev-machine fetch of the NHTSA vPIC (Vehicle Product Information
Catalog) public-domain make/model data — the primary layer of F4's vehicle
seed dataset. Run this once, online, and commit its output
(vpic_raw.json); nothing at the show ever calls this or needs network
access — see CONTEXT.md's offline-first rule and DECISIONS.md's F4 entry.

Source: https://vpic.nhtsa.dot.gov/api/ — a U.S. government API (NHTSA,
part of the Department of Transportation), public domain (17 U.S.C. 105:
U.S. Government works are not subject to copyright in the United States).

Process: pulls the make list for the three vehicle types relevant to a car
show (Car, Truck, Multipurpose Passenger Vehicle), dedupes by name, then
pulls the full model list for every one of those makes via
GetModelsForMake (all model years vPIC has, not scoped to one year).
Motorcycles are deliberately NOT pulled from vPIC here — spot-checked and
found to be dominated by one-off custom/chopper builders (see
DECISIONS.md) — motorcycles instead come from the small curated list in
build_vehicle_seed.py.
"""
import json
import time
import urllib.parse
import urllib.request

BASE = "https://vpic.nhtsa.dot.gov/api/vehicles"
VEHICLE_TYPES = ["car", "truck", "multipurpose passenger vehicle (mpv)"]


def fetch(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=20) as resp:
        return json.load(resp)


def get_makes_for_type(vtype: str) -> list[str]:
    url = f"{BASE}/GetMakesForVehicleType/{urllib.parse.quote(vtype)}?format=json"
    data = fetch(url)
    return [r["MakeName"].strip() for r in data["Results"]]


def get_models_for_make(make: str) -> list[str]:
    url = f"{BASE}/GetModelsForMake/{urllib.parse.quote(make)}?format=json"
    data = fetch(url)
    return sorted({r["Model_Name"].strip() for r in data["Results"] if r["Model_Name"]})


def main():
    makes: set[str] = set()
    for vtype in VEHICLE_TYPES:
        names = get_makes_for_type(vtype)
        print(f"{vtype}: {len(names)} makes")
        makes.update(names)

    makes_sorted = sorted(makes)
    print(f"Total unique makes across car/truck/mpv: {len(makes_sorted)}")

    result = {}
    for i, make in enumerate(makes_sorted):
        try:
            models = get_models_for_make(make)
        except Exception as e:
            print(f"  [{i+1}/{len(makes_sorted)}] {make}: FAILED ({e})")
            models = []
        result[make] = models
        if (i + 1) % 25 == 0:
            print(f"  ...{i+1}/{len(makes_sorted)} makes fetched")
        time.sleep(0.05)  # polite pacing against the public API

    out_path = "firmware/tools/vpic_raw.json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1, sort_keys=True)
    print(f"Wrote {out_path}: {len(result)} makes, {sum(len(v) for v in result.values())} models total")


if __name__ == "__main__":
    main()
