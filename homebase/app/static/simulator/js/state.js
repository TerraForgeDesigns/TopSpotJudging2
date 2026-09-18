// localStorage-backed persistence — the simulator's stand-in for the
// real device's SD card. One key per "file," mirroring the exact set of
// things firmware persists and for the exact same reason: survive a
// reload the way a real handheld survives a restart. See:
//   storage::SyncState      -> "sim_sync_state"
//   storage::ShowInfo       -> "sim_show_info"
//   storage::Entry[]        -> "sim_entries"
//   storage::QueuedCar[]    -> "sim_queue"
//   storage::DraftCar       -> "sim_draft"
//   vehicle recents         -> "sim_vehicle_recents"
//   storage::Settings       -> "sim_settings"
// Every getter returns a fresh default shape if nothing's stored yet —
// same "missing preference is never a startup failure" rule
// storage/settings.h documents.

function load(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch {
    return fallback;
  }
}
function save(key, value) {
  localStorage.setItem(key, JSON.stringify(value));
}

// ---- Settings (storage/settings.h) --------------------------------------

const SETTINGS_KEY = "sim_settings";
const DEFAULT_SETTINGS = {
  handheldLabel: "sim-1",
  judgeName: "",
  theme: "dark",
  syncIntervalSeconds: 180,
  // Simulator-only (task requirement 6) — no WiFi-scan analog needed
  // since the simulator always talks same-origin; this flag alone drives
  // sync.js's "treat every attempt as a scan miss" behavior.
  outOfRange: false,
  usePhysicalKeyboard: false,
};

export function getSettings() {
  return { ...DEFAULT_SETTINGS, ...load(SETTINGS_KEY, {}) };
}
export function saveSettings(settings) {
  save(SETTINGS_KEY, settings);
}

// ---- Sync state (storage/sync_state.h) -----------------------------------

const SYNC_STATE_KEY = "sim_sync_state";
const DEFAULT_SYNC_STATE = {
  lastConfigRevisionApplied: 0,
  lastDataRevisionApplied: 0,
  lastSuccessfulUpdateEpochSeconds: 0,
  currentRetryIntervalSeconds: 180,
  totalCars: 0,
  judgedCars: 0,
  unjudgedCars: 0,
  flaggedConflictCars: 0,
};

export function getSyncState() {
  return { ...DEFAULT_SYNC_STATE, ...load(SYNC_STATE_KEY, {}) };
}
export function saveSyncState(state) {
  save(SYNC_STATE_KEY, state);
}

// ---- Show info (storage/show_data.h) -------------------------------------

const SHOW_INFO_KEY = "sim_show_info";
const DEFAULT_SHOW_INFO = {
  showId: 0,
  showName: "",
  eventDate: "",
  eventYear: 0,
  scoreRangeMax: 5,
  maxScore: 0,
  overallImpressionEnabled: false,
  categories: [],
  judgeChosenAwards: [],
};

export function getShowInfo() {
  return { ...DEFAULT_SHOW_INFO, ...load(SHOW_INFO_KEY, {}) };
}
export function saveShowInfo(info) {
  save(SHOW_INFO_KEY, info);
}

// ---- Entries cache (storage::Entry[]) ------------------------------------

const ENTRIES_KEY = "sim_entries";

export function getEntries() {
  return load(ENTRIES_KEY, []);
}
export function findEntry(entryNumber) {
  return getEntries().find((e) => e.entry_number === entryNumber) || null;
}
export function mergeEntries(delta, fullSnapshot = false) {
  if (!delta || (delta.length === 0 && !fullSnapshot)) return;
  const existing = fullSnapshot ? [] : getEntries();
  const byNumber = new Map(existing.map((e) => [e.entry_number, e]));
  for (const e of delta) byNumber.set(e.entry_number, e);
  save(ENTRIES_KEY, Array.from(byNumber.values()));
}

// ---- Finished-car queue (storage/pending_queue.h) ------------------------

const QUEUE_KEY = "sim_queue";

export function getQueue() {
  return load(QUEUE_KEY, []);
}
export function enqueueCar(car) {
  const queue = getQueue();
  queue.push(car);
  save(QUEUE_KEY, queue);
}
export function removeFromQueue(entryNumber) {
  save(QUEUE_KEY, getQueue().filter((c) => c.entry_number !== entryNumber));
}
export function isEntryQueued(entryNumber) {
  return getQueue().some((c) => c.entry_number === entryNumber);
}

// ---- In-progress draft (storage/drafts.h) --------------------------------

const DRAFT_KEY = "sim_draft";

export function getDraft() {
  return load(DRAFT_KEY, null);
}
export function saveDraft(draft) {
  save(DRAFT_KEY, draft);
}
export function clearDraft() {
  localStorage.removeItem(DRAFT_KEY);
}

// ---- Vehicle recents (storage/vehicle_recents.h) -------------------------

const RECENTS_KEY = "sim_vehicle_recents";
const MAX_RECENT_MAKES = 20;
const MAX_RECENT_MODELS = 10;

function getRecentsRaw() {
  const showId = getShowInfo().showId;
  const stored = load(RECENTS_KEY, null);
  // Scoped by Show ID, not name — a mismatch means "new show," recents
  // reset. Same rule storage/vehicle_recents.h documents.
  if (!stored || stored.showId !== showId) {
    return { showId, makes: [], models: {} };
  }
  return stored;
}

function bumpToFront(list, value, max) {
  const filtered = list.filter((v) => v.toLowerCase() !== value.toLowerCase());
  filtered.unshift(value);
  return filtered.slice(0, max);
}

export function recordMakeUsed(make) {
  const recents = getRecentsRaw();
  recents.makes = bumpToFront(recents.makes, make, MAX_RECENT_MAKES);
  save(RECENTS_KEY, recents);
}
export function recordModelUsed(make, model) {
  const recents = getRecentsRaw();
  const forMake = recents.models[make] || [];
  recents.models[make] = bumpToFront(forMake, model, MAX_RECENT_MODELS);
  save(RECENTS_KEY, recents);
}
export function getRecentMakes() {
  return getRecentsRaw().makes;
}
export function getRecentModels(make) {
  return getRecentsRaw().models[make] || [];
}
