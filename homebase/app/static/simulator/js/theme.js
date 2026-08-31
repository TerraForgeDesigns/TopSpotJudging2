// Theme toggle — mirrors ui/theme.cpp's setMode()/currentMode()/persist()
// exactly in spirit: default Dark, persisted (localStorage here, standing
// in for the real device's /prefs/theme.txt on SD — same "survive a
// restart, default to Dark if unreadable" resilience rule), applied by
// setting a data-theme attribute the CSS in simulator.css keys off of.
const STORAGE_KEY = "sim_theme";

export function currentTheme() {
  const stored = localStorage.getItem(STORAGE_KEY);
  return stored === "daylight" ? "daylight" : "dark";
}

export function setTheme(mode) {
  const value = mode === "daylight" ? "daylight" : "dark";
  document.documentElement.setAttribute("data-theme", value);
  localStorage.setItem(STORAGE_KEY, value);
}

export function initTheme() {
  setTheme(currentTheme());
}
