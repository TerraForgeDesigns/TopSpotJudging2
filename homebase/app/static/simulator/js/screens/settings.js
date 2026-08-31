// SETTINGS — the subset settings_screen.cpp exposes that has a simulator
// equivalent: theme toggle, sync interval, and two simulator-only
// controls the task itself asks for (Out of Range, task requirement 6;
// Use physical keyboard, task requirement 3). No WiFi SSID/password row
// (the simulator always talks same-origin) and no Diagnostics screen
// (out of scope for this task).
import * as screens from "../screens.js";
import * as state from "../state.js";
import { setTheme } from "../theme.js";
import { showToast } from "../toast.js";

function sectionLabel(content, text) {
  const el = document.createElement("div");
  el.className = "text-secondary";
  el.style.fontWeight = "500";
  el.style.fontSize = "13px";
  el.style.marginTop = "16px";
  el.textContent = text;
  content.appendChild(el);
}

function toggleRow(content, label, checked, onChange) {
  const row = document.createElement("label");
  row.className = "list-row";
  row.style.display = "flex";
  row.style.justifyContent = "space-between";
  row.style.alignItems = "center";
  row.style.cursor = "pointer";

  const text = document.createElement("span");
  text.textContent = label;

  const input = document.createElement("input");
  input.type = "checkbox";
  input.checked = checked;
  input.addEventListener("change", () => onChange(input.checked));

  row.append(text, input);
  content.appendChild(row);
  return input;
}

export function build(content) {
  const settings = state.getSettings();

  sectionLabel(content, "DISPLAY");
  const themeRow = document.createElement("div");
  themeRow.className = "list-row";
  themeRow.style.display = "flex";
  themeRow.style.justifyContent = "space-between";
  themeRow.style.alignItems = "center";

  const themeLabel = document.createElement("span");
  themeLabel.textContent = "Theme";

  const themeSelect = document.createElement("select");
  for (const [value, text] of [["dark", "Dark"], ["daylight", "Daylight"]]) {
    const opt = document.createElement("option");
    opt.value = value;
    opt.textContent = text;
    if (settings.theme === value) opt.selected = true;
    themeSelect.appendChild(opt);
  }
  themeSelect.addEventListener("change", () => {
    const s = state.getSettings();
    s.theme = themeSelect.value;
    state.saveSettings(s);
    setTheme(themeSelect.value);
  });
  themeRow.append(themeLabel, themeSelect);
  content.appendChild(themeRow);

  sectionLabel(content, "SYNC");
  const intervalRow = document.createElement("div");
  intervalRow.className = "list-row";
  intervalRow.style.display = "flex";
  intervalRow.style.justifyContent = "space-between";
  intervalRow.style.alignItems = "center";

  const intervalLabel = document.createElement("span");
  intervalLabel.textContent = "Sync interval (seconds)";

  const intervalInput = document.createElement("input");
  intervalInput.type = "number";
  intervalInput.min = "15";
  intervalInput.max = "900";
  intervalInput.value = settings.syncIntervalSeconds;
  intervalInput.style.width = "80px";
  intervalInput.addEventListener("change", () => {
    const value = Math.min(900, Math.max(15, parseInt(intervalInput.value, 10) || 180));
    intervalInput.value = value;
    const s = state.getSettings();
    s.syncIntervalSeconds = value;
    state.saveSettings(s);
    showToast("Sync interval updated.", "success", 2000);
  });
  intervalRow.append(intervalLabel, intervalInput);
  content.appendChild(intervalRow);

  toggleRow(content, "Out of Range (simulate no connectivity)", settings.outOfRange, (checked) => {
    const s = state.getSettings();
    s.outOfRange = checked;
    state.saveSettings(s);
    showToast(checked ? "Out of Range: sync attempts will now fail like a missed WiFi scan." : "Out of Range disabled: sync attempts will reach Home Base again.", checked ? "error" : "success", 3000);
  });

  sectionLabel(content, "INPUT (SIMULATOR ONLY)");
  toggleRow(content, "Use physical keyboard", settings.usePhysicalKeyboard, (checked) => {
    const s = state.getSettings();
    s.usePhysicalKeyboard = checked;
    state.saveSettings(s);
  });
  const hint = document.createElement("div");
  hint.className = "text-secondary";
  hint.style.fontSize = "12px";
  hint.textContent = "Off by default. The on-screen keyboard and keypad are the real device's input path — this is a developer convenience only, and typed keys are routed through the same on-screen key handlers either way.";
  content.appendChild(hint);

  sectionLabel(content, "DEVICE");
  const labelRow = document.createElement("div");
  labelRow.className = "list-row";
  labelRow.style.display = "flex";
  labelRow.style.justifyContent = "space-between";
  labelRow.style.alignItems = "center";
  const labelLabel = document.createElement("span");
  labelLabel.textContent = "Handheld label";
  const labelInput = document.createElement("input");
  labelInput.type = "text";
  labelInput.value = settings.handheldLabel;
  labelInput.style.width = "140px";
  labelInput.addEventListener("change", () => {
    const s = state.getSettings();
    s.handheldLabel = labelInput.value || "sim-1";
    state.saveSettings(s);
  });
  labelRow.append(labelLabel, labelInput);
  content.appendChild(labelRow);
}

export function buildSettingsScreen(content) {
  return build(content);
}
