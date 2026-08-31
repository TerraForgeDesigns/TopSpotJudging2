"""Cross-checks every model name in curated_classic.json against its make's
Wikipedia "List of <Make> vehicles" page (or best-guess equivalent title) —
the documented-provenance requirement for F4's curated-classic supplemental
layer (see DECISIONS.md's F4 entry and curated_classic.json's own
_provenance note). A model name only counts as verified if it's found
verbatim (case-insensitive) in that page's raw wikitext. This is a
dev-machine, one-time check — not something the firmware or any runtime
component depends on.

Only the bare FACT "this make produced a model with this name" is being
checked here — Wikipedia's own prose is not copied into the seed data.
"""
import json
import time
import urllib.error
import urllib.parse
import urllib.request

PAGE_TITLES = {
    "Chevrolet": "List of Chevrolet vehicles",
    "Ford": "List of Ford vehicles",
    "Pontiac": "List of Pontiac vehicles",
    "Dodge": "List of Dodge vehicles",
    "Plymouth": "List of Plymouth vehicles",
    "Oldsmobile": "List of Oldsmobile vehicles",
    "Buick": "List of Buick vehicles",
    "AMC": "American Motors Corporation",
    "Studebaker": "Studebaker",
    "Mercury": "List of Mercury vehicles",
    "Lincoln": "List of Lincoln vehicles",
    "Chrysler": "List of Chrysler vehicles",
    "Nash": "Nash Motors",
    "Hudson": "Hudson Motor Car Company",
    "Packard": "Packard",
    "DeSoto": "DeSoto (automobile)",
    "Willys": "Willys",
    "International Harvester": "International Harvester",
    "Checker": "Checker Motors Corporation",
    "Harley-Davidson": "List of Harley-Davidson motorcycles",
    "Indian": "Indian Motorcycle",
    "Triumph": "Triumph Motorcycles Ltd",
    "Honda": "List of Honda motorcycles",
    "Yamaha": "List of Yamaha motorcycles",
    "Kawasaki": "List of Kawasaki motorcycles",
    "Suzuki": "List of Suzuki motorcycles",
    "Ducati": "Ducati",
    "BMW": "List of BMW motorcycles",
    "Victory Motorcycles": "Victory Motorcycles",
    "Moto Guzzi": "Moto Guzzi",
    "Royal Enfield": "Royal Enfield",
    "Vespa": "Vespa",
}


def fetch_wikitext(title: str) -> str:
    url = "https://en.wikipedia.org/w/index.php?" + urllib.parse.urlencode({"title": title, "action": "raw"})
    req = urllib.request.Request(url, headers={"User-Agent": "TopSpotJudging-DevTool/1.0 (research)"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        return resp.read().decode("utf-8", errors="replace")


def resolve_title(query: str) -> str | None:
    """Wikipedia's search API — used when a hardcoded PAGE_TITLES guess 404s,
    so a title drift doesn't just silently fail the whole make's check."""
    url = "https://en.wikipedia.org/w/api.php?" + urllib.parse.urlencode(
        {"action": "query", "list": "search", "srsearch": query, "format": "json", "srlimit": 1}
    )
    req = urllib.request.Request(url, headers={"User-Agent": "TopSpotJudging-DevTool/1.0 (research)"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        data = json.load(resp)
    hits = data.get("query", {}).get("search", [])
    return hits[0]["title"] if hits else None


def main():
    src = json.load(open("firmware/tools/curated_classic.json", encoding="utf-8"))
    entries = [(m["make"], m["models"]) for m in src["makes"]] + [(m["make"], m["models"]) for m in src["motorcycle_makes"]]

    page_cache: dict[str, str] = {}
    total = 0
    verified = 0
    unverified: list[tuple[str, str]] = []

    for make, models in entries:
        title = PAGE_TITLES.get(make)
        if title is None:
            print(f"[SKIP] no page mapping for make {make!r}")
            continue
        if title not in page_cache:
            try:
                page_cache[title] = fetch_wikitext(title)
            except urllib.error.HTTPError as e:
                resolved = resolve_title(title)
                if resolved and resolved != title:
                    print(f"[RETRY] {title!r} -> search resolved to {resolved!r}")
                    try:
                        page_cache[title] = fetch_wikitext(resolved)
                    except urllib.error.HTTPError as e2:
                        print(f"[FAIL] {resolved}: HTTP {e2.code}")
                        page_cache[title] = ""
                else:
                    print(f"[FAIL] {title}: HTTP {e.code}, no search match")
                    page_cache[title] = ""
            time.sleep(0.1)
        text = page_cache[title].lower()

        for model in models:
            total += 1
            if model.lower() in text:
                verified += 1
            else:
                unverified.append((make, model))

    print(f"\n{verified}/{total} curated-classic model names verified against their make's Wikipedia page.")
    if unverified:
        print("UNVERIFIED (not found verbatim — review before shipping):")
        for make, model in unverified:
            print(f"  {make}: {model!r} (page: {PAGE_TITLES.get(make)})")


if __name__ == "__main__":
    main()
