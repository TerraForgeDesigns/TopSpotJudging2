"""
A headless handheld simulator — impersonates a real ESP32 device against
the real POST /api/v1/sync endpoint (see PROTOCOL.md) without any of the
four physical handhelds. `SimulatedHandheld` wraps any client exposing a
`.post(path, json=...)` method returning something with `.status_code`
and `.json()` — in practice the `client` fixture from conftest.py (a
TestClient against the real FastAPI app and a real, if in-memory,
database — see that fixture's own docstring: this project already treats
that as "the real API," not a mock, the same standard
tests/test_sync_idempotency.py's docstring states explicitly).

Deliberately framework-free (no pytest import here) — this is a plain
Python class that reads as "a simulated device," reusable by any test
file, not test-helper soup living inside one test module.

Mirrors PROTOCOL.md's trigger model and idempotency rules exactly, the
same way firmware/src/network/wifi_sync.cpp and the SIM1 browser
simulator's static/simulator/js/sync.js do for their platforms — this is
the third independent implementation of the same contract, in Python,
which is itself a real cross-check that the contract is unambiguous.
"""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class SimulatedHandheld:
    client: object
    handheld_id: str
    battery_pct: int | None = 80

    config_revision: int = 0
    data_revision: int = 0
    out_of_range: bool = False

    queue: list[dict] = field(default_factory=list)
    configuration: dict | None = None
    roster: dict[str, dict] = field(default_factory=dict)
    summary: dict | None = None
    last_response: dict | None = None

    _uptime_ms: int = 0

    def _next_uptime_ms(self) -> int:
        self._uptime_ms += 1000
        return self._uptime_ms

    def category_id(self, category_name: str) -> int:
        if not self.configuration:
            raise RuntimeError(
                f"{self.handheld_id}: call check_in() at least once before scoring — a real "
                "handheld can't score against categories it hasn't learned yet."
            )
        for category in self.configuration["categories"]:
            if category["name"] == category_name:
                return category["id"]
        raise KeyError(f"No active Judging Category named {category_name!r} in this handheld's configuration.")

    def award_id(self, award_name: str) -> int:
        if not self.configuration:
            raise RuntimeError(f"{self.handheld_id}: call check_in() before nominating for an award.")
        for award in self.configuration["judge_chosen_awards"]:
            if award["name"] == award_name:
                return award["id"]
        raise KeyError(f"No judge-chosen award named {award_name!r} in this handheld's configuration.")

    def _build_submission(
        self,
        entry_number: str,
        scores: dict[str, int],
        *,
        participant: str | None = None,
        year: str | None = None,
        make: str | None = None,
        model: str | None = None,
        vehicle_type: str | None = None,
        make_manually_entered: bool = False,
        model_manually_entered: bool = False,
        overall_impression: int | None = None,
        nominations: list[str] | None = None,
        judge_name: str | None = None,
        score_range_max: int | None = None,
    ) -> dict:
        if self.configuration is None:
            raise RuntimeError(f"{self.handheld_id}: call check_in() before judge_car() — see category_id().")
        closed_at_uptime_ms = self._next_uptime_ms()
        photo_token = f"{entry_number}_{closed_at_uptime_ms:08d}"
        return {
            "entry_number": entry_number,
            "judge_name": judge_name,
            "closed_at_uptime_ms": closed_at_uptime_ms,
            "participant": participant,
            "year": year,
            "make": make,
            "model": model,
            "vehicle_type": vehicle_type,
            "make_manually_entered": make_manually_entered,
            "model_manually_entered": model_manually_entered,
            "score_range_max": score_range_max if score_range_max is not None else self.configuration["score_range_max"],
            "scores": [{"category_id": self.category_id(name), "points": points} for name, points in scores.items()],
            "overall_impression": overall_impression,
            "nominations": [self.award_id(name) for name in (nominations or [])],
            "vehicle_photo_path": f"/sdcard/topspot/photos/vehicle/vehicle_{photo_token}.jpg",
            "judge_sheet_photo_path": f"/sdcard/topspot/photos/judge_sheets/judge_sheet_{photo_token}.jpg",
        }

    def _remove_from_queue(self, item: dict) -> None:
        key = (item["entry_number"], item["closed_at_uptime_ms"])
        self.queue = [q for q in self.queue if (q["entry_number"], q["closed_at_uptime_ms"]) != key]

    def _post(self, submissions: list[dict]) -> dict:
        payload = {
            "handheld_id": self.handheld_id,
            "show_id": self.configuration["show_id"] if self.configuration else 0,
            "config_revision": self.config_revision,
            "data_revision": self.data_revision,
            "known_car_count": len(self.roster),
            "battery_pct": self.battery_pct,
            "submissions": submissions,
        }
        response = self.client.post("/api/v1/sync", json=payload)
        assert response.status_code == 200, response.text
        return response.json()

    def check_in(self, extra_submissions: list[dict] | None = None) -> dict | None:
        """The one routine both PROTOCOL.md triggers funnel through. Out of
        range mirrors "SSID not found -> sleep immediately, queue intact":
        no network call at all, nothing about local state changes."""
        if self.out_of_range:
            return None

        submitted = list(self.queue) + list(extra_submissions or [])
        data = self._post(submitted)

        self.config_revision = data["config_revision"]
        self.data_revision = data["data_revision"]
        if data.get("configuration") is not None:
            self.configuration = data["configuration"]
        if data.get("sync_mode") == "FULL":
            self.roster = {}
        for car in data.get("cars", []):
            self.roster[car["entry_number"]] = car
        self.summary = data.get("summary")

        for item, result in zip(submitted, data.get("results", [])):
            # accepted/already_recorded: done. flagged_duplicate: this
            # handheld lost the fight, nothing left to retry. "error"
            # deliberately stays queued — matches the firmware's own
            # sync.js reference behavior (see this file's docstring).
            if result["status"] in ("accepted", "already_recorded", "flagged_duplicate"):
                self._remove_from_queue(item)

        self.last_response = data
        return data

    def periodic_tick(self) -> dict | None:
        """Trigger (b): the periodic timer firing with nothing new to
        push — PROTOCOL.md is explicit this is valid and expected, purely
        to refresh server_time/config/roster/summary."""
        return self.check_in()

    def go_out_of_range(self) -> None:
        self.out_of_range = True

    def come_back_into_range(self) -> None:
        self.out_of_range = False

    def judge_car(self, entry_number: str, scores: dict[str, int], **kwargs) -> dict | None:
        """Trigger (a): a judge closes out a car. Queues the submission,
        then attempts sync immediately, ignoring backoff — unless out of
        range, in which case it just joins the queue like a real
        handheld's would."""
        submission = self._build_submission(entry_number, scores, **kwargs)
        self.queue.append(submission)
        if self.out_of_range:
            return None
        return self.check_in()

    def judge_car_losing_the_ack(self, entry_number: str, scores: dict[str, int], **kwargs) -> dict:
        """Simulates a submission the SERVER genuinely recorded, but whose
        response never reached the handheld (WiFi dropped mid-ack, power
        loss, etc.). The submission stays in .queue exactly as a real
        handheld's would, since it never got confirmation — a later
        check_in() will resend the identical (entry_number, handheld_id,
        closed_at_uptime_ms) triple, and PROTOCOL.md's idempotency rule
        makes the server return "already_recorded" that time, only then
        clearing the queue. This method deliberately does NOT apply the
        response or touch revision counters — that's the whole point."""
        submission = self._build_submission(entry_number, scores, **kwargs)
        self.queue.append(submission)
        return self._post([submission])
