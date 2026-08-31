"""Merges the two documented sources — NHTSA vPIC's live-fetched car/truck/
MPV data (vpic_raw.json, produced by fetch_vpic.py) and the curated-classic
supplement (curated_classic.json, cross-checked by
verify_curated_classic.py) — into firmware/tools/vehicle_seed_source.json,
the single provenance-tagged input build_vehicle_seed.py turns into the
firmware binary. See DECISIONS.md's F4 entry for the full provenance
writeup; this script only merges, it does not invent any new data.

Merge rule: a curated-classic/motorcycle make that matches an existing
vPIC make case-insensitively has its models MERGED into that same make
entry (deduplicated, case-insensitive) — never a second entry for the same
real-world make (e.g. exactly one "Honda", carrying both its vPIC car
models and its curated-classic motorcycle models). A curated make with no
vPIC match becomes its own new entry.

Also enforces the two hard constraints every downstream consumer relies
on: ASCII-only (matches the embedded fonts' ASCII-only glyph range, see
ui/fonts/fonts.h) and each name <=47 bytes (storage::DraftCar.make/model
are char[48] — see storage/drafts.h).
"""
import json


MAX_NAME_LEN = 47

skipped: list[str] = []


def validate_name(name: str, context: str) -> str:
    """Raises for a make name (a structural problem worth a hard stop —
    there are few makes and each is deliberately chosen). Returns None for
    a model name that fails the same checks (there are thousands, sourced
    live from vPIC's broader VIN-decode registry, which mixes in some
    upfitter/trailer-manufacturer noise under odd make groupings) — the
    caller skips it and this function records why, so nothing is silently
    dropped without a paper trail in the tool's own output."""
    name = name.strip()
    if not name:
        return None
    if not name.isascii():
        skipped.append(f"{context}: non-ASCII {name!r}")
        return None
    if len(name.encode("ascii")) > MAX_NAME_LEN:
        skipped.append(f"{context}: {name!r} exceeds {MAX_NAME_LEN} bytes")
        return None
    return name


def validate_make_name(name: str) -> str:
    name = name.strip()
    if not name:
        raise ValueError("empty make name")
    if not name.isascii():
        raise ValueError(f"non-ASCII make name {name!r}")
    if len(name.encode("ascii")) > MAX_NAME_LEN:
        raise ValueError(f"make name {name!r} exceeds {MAX_NAME_LEN} bytes")
    return name


def main():
    vpic = json.load(open("firmware/tools/vpic_raw.json", encoding="utf-8"))
    curated = json.load(open("firmware/tools/curated_classic.json", encoding="utf-8"))

    # make_name_lower -> {"make": display name, "models": {model_lower: display name}, "source": set}
    merged: dict[str, dict] = {}

    def add_make(make: str, source: str):
        make = validate_make_name(make)
        key = make.lower()
        if key not in merged:
            merged[key] = {"make": make, "models": {}, "sources": set()}
        merged[key]["sources"].add(source)
        return merged[key]

    def add_model(entry: dict, model: str, source: str):
        model = validate_name(model, f"model of {entry['make']}")
        if model is None:
            return
        mkey = model.lower()
        if mkey not in entry["models"]:
            entry["models"][mkey] = {"name": model, "sources": set()}
        entry["models"][mkey]["sources"].add(source)

    for make, models in vpic.items():
        entry = add_make(make, "nhtsa-vpic")
        for model in models:
            add_model(entry, model, "nhtsa-vpic")

    for group in ("makes", "motorcycle_makes"):
        for item in curated[group]:
            entry = add_make(item["make"], "curated-classic")
            for model in item["models"]:
                add_model(entry, model, "curated-classic")

    makes_out = []
    total_models = 0
    for key in sorted(merged.keys()):
        entry = merged[key]
        models_out = [
            {"name": m["name"], "source": "+".join(sorted(m["sources"]))}
            for m in sorted(entry["models"].values(), key=lambda m: m["name"].lower())
        ]
        total_models += len(models_out)
        makes_out.append(
            {
                "make": entry["make"],
                "source": "+".join(sorted(entry["sources"])),
                "models": models_out,
            }
        )

    out = {
        "provenance": {
            "nhtsa-vpic": {
                "source": "NHTSA vPIC (Vehicle Product Information Catalog) API",
                "url": "https://vpic.nhtsa.dot.gov/api/",
                "license": "U.S. Government work — public domain (17 U.S.C. 105)",
                "retrieved_by": "firmware/tools/fetch_vpic.py",
                "filter": "GetMakesForVehicleType: car, truck, multipurpose passenger vehicle (mpv); "
                "models via GetModelsForMake (all model years). Motorcycle type deliberately excluded — "
                "spot-checked and found dominated by one-off custom/chopper builders.",
            },
            "curated-classic": {
                "source": "Hand-compiled from well-established automotive history "
                "(pre-1980s/discontinued makes vPIC under-covers, plus motorcycle makes)",
                "verification": "Cross-checked against each make's Wikipedia page via "
                "firmware/tools/verify_curated_classic.py — every model name confirmed present "
                "(verbatim or via its own dedicated model page) before inclusion. Only bare facts "
                "(a make produced a model with this name) are used, not Wikipedia's prose.",
            },
        },
        "makes": makes_out,
    }

    with open("firmware/tools/vehicle_seed_source.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)

    print(f"Merged: {len(makes_out)} makes, {total_models} models -> firmware/tools/vehicle_seed_source.json")
    if skipped:
        print(f"Skipped {len(skipped)} model name(s) that failed ASCII/length validation (source data noise, not curated data):")
        for s in skipped[:20]:
            print(f"  {s}")
        if len(skipped) > 20:
            print(f"  ...and {len(skipped) - 20} more")


if __name__ == "__main__":
    main()
