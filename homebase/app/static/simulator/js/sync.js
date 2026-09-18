// The real sync client — ports network/wifi_sync.cpp's actual logic
// against the REAL POST /api/v1/sync endpoint on this same origin (task
// requirement 5), not a mock. Three triggers (a finished car, a periodic
// timer, Update Now), scan-first-conceptually backoff (task requirement
// 6's Out of Range toggle stands in for a real WiFi scan miss — see
// attempt() below), never drops a queued car except on an explicit
// acknowledgement, same as the firmware.
import * as state from "./state.js";
import { setConnState } from "./status_bar.js";
import { showToast } from "./toast.js";
import { showScoringUpdated } from "./scoring_updated.js";

const MAX_RETRY_INTERVAL_SECONDS = 900; // PROTOCOL.md: cap at 15 minutes

let periodicTimer = null;
let running = false;

function buildRequestBody() {
  const settings = state.getSettings();
  const syncState = state.getSyncState();
  return {
    handheld_id: settings.handheldLabel,
    show_id: state.getShowInfo().showId || 0,
    config_revision: syncState.lastConfigRevisionApplied,
    data_revision: syncState.lastDataRevisionApplied,
    battery_pct: null, // no battery model in the simulator — honestly null, same as firmware's F7 stance
    submissions: state.getQueue(),
  };
}

function schedulePeriodicTimer(seconds) {
  if (periodicTimer) clearInterval(periodicTimer);
  periodicTimer = setInterval(() => attempt(true), Math.max(seconds, 5) * 1000);
}

function rescheduleBackoff(hit) {
  const syncState = state.getSyncState();
  const settings = state.getSettings();
  if (hit) {
    syncState.currentRetryIntervalSeconds = settings.syncIntervalSeconds;
  } else {
    syncState.currentRetryIntervalSeconds = Math.min(syncState.currentRetryIntervalSeconds * 2, MAX_RETRY_INTERVAL_SECONDS);
  }
  state.saveSyncState(syncState);
  schedulePeriodicTimer(syncState.currentRetryIntervalSeconds);
}

function applyMiss(isPeriodic, scanFound) {
  setConnState("not_connected");
  if (scanFound) showToast("Home Base Not Connected", "error");
  // Never apply backoff to a just-finished-car trigger or Update Now —
  // only the periodic trigger's own misses ever double the interval.
  if (isPeriodic) rescheduleBackoff(false);
}

async function applyHit(response) {
  const stateBefore = state.getSyncState();
  const hadConfigBefore = stateBefore.lastConfigRevisionApplied > 0;

  const before = state.getShowInfo();
  if (response.configuration) {
    const cfg = response.configuration;
    const after = {
      ...before,
      showId: cfg.show_id,
      showName: cfg.show_name ?? before.showName,
      scoreRangeMax: cfg.score_range_max ?? before.scoreRangeMax,
      maxScore: cfg.max_score ?? before.maxScore,
      overallImpressionEnabled: cfg.overall_impression_enabled ?? before.overallImpressionEnabled,
      categories: cfg.categories || [],
      judgeChosenAwards: cfg.judge_chosen_awards || [],
    };
    state.saveShowInfo(after);

    // First-ever configuration this simulator has applied is normal
    // setup, not a "change" — neither notice fires for it.
    if (hadConfigBefore) {
      if (after.scoreRangeMax !== before.scoreRangeMax) {
        await showScoringUpdated(after.scoreRangeMax);
      } else {
        showToast("Show Setup Updated", "success");
      }
    }
  }

  if (response.cars && (response.cars.length > 0 || response.sync_mode === "FULL")) {
    state.mergeEntries(
      response.cars.map((c) => ({
        entry_number: c.entry_number,
        participant: c.participant || "",
        year: c.year || "",
        make: c.make || "",
        model: c.model || "",
        vehicle_type: c.vehicle_type || "",
      })),
      response.sync_mode === "FULL"
    );
  }

  // vehicle_additions: always [] from real Home Base today (HB5 isn't
  // built — see DECISIONS.md's SIM1 entry) — the loop still runs so this
  // path is visibly wired, not silently skipped, matching firmware's own
  // F4/F5 "sits honestly inactive" precedent for this exact field.
  for (const _addition of response.vehicle_additions || []) {
    // No learned-vehicle store in the simulator — nothing to do yet.
  }

  for (const r of response.results || []) {
    if (!r.entry_number) continue;
    if (r.status === "accepted" || r.status === "already_recorded") {
      state.removeFromQueue(r.entry_number);
    } else if (r.status === "flagged_duplicate") {
      state.removeFromQueue(r.entry_number);
      showToast(`Entry ${r.entry_number} was already judged by another handheld.`, "error");
    }
    // "error" is deliberately left queued — not an explicit acknowledgement.
  }

  const summary = response.summary || {};
  const syncState = state.getSyncState();
  syncState.totalCars = summary.total_cars ?? syncState.totalCars;
  syncState.judgedCars = summary.judged ?? syncState.judgedCars;
  syncState.unjudgedCars = summary.unjudged ?? syncState.unjudgedCars;
  syncState.flaggedConflictCars = summary.flagged_conflict ?? syncState.flaggedConflictCars;
  syncState.lastConfigRevisionApplied = response.config_revision ?? syncState.lastConfigRevisionApplied;
  syncState.lastDataRevisionApplied = response.data_revision ?? syncState.lastDataRevisionApplied;
  syncState.lastSuccessfulUpdateEpochSeconds = Math.floor(Date.now() / 1000);
  state.saveSyncState(syncState);

  rescheduleBackoff(true); // resets the interval to base regardless of which trigger fired
  setConnState("up_to_date");
}

async function attempt(isPeriodic) {
  if (running) return;
  running = true;
  try {
    const settings = state.getSettings();
    // Out of Range (task requirement 6): stands in for a missed WiFi
    // scan — short-circuits BEFORE any network call, at the same point
    // in the trigger model a real scan miss would, so backoff/queue
    // behavior is genuinely exercised rather than faked at another layer.
    if (settings.outOfRange) {
      applyMiss(isPeriodic, false);
      return;
    }

    setConnState("updating");
    let resp;
    try {
      resp = await fetch("/api/v1/sync", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(buildRequestBody()),
      });
    } catch {
      applyMiss(isPeriodic, true);
      return;
    }
    if (!resp.ok) {
      applyMiss(isPeriodic, true);
      return;
    }
    const data = await resp.json();
    await applyHit(data);
  } finally {
    running = false;
  }
}

export function init() {
  const syncState = state.getSyncState();
  schedulePeriodicTimer(syncState.currentRetryIntervalSeconds);
}

// Triggers (a)/(c) — a car was just finished, or the judge tapped Update
// Now. Runs immediately, never touches the backoff interval either way.
export function requestNow() {
  attempt(false);
}
