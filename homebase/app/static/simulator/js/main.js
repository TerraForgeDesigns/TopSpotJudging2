// Entry point — wires the device frame's DOM elements to screens.js/
// status_bar.js/theme.js, starts sync.js's periodic timer (trigger b),
// and pushes Home as the root screen. Mirrors main.cpp's own startup
// order: theme, then screen manager, then network, then the first screen.
import { initTheme } from "./theme.js";
import * as screens from "./screens.js";
import { initStatusBar, setDeviceLabel, setJudgeLabel } from "./status_bar.js";
import * as state from "./state.js";
import * as sync from "./sync.js";
import { build as buildHome } from "./screens/home.js";

initTheme();

initStatusBar({
  deviceLabel: document.getElementById("device-label"),
  judgeLabel: document.getElementById("judge-label"),
  connDot: document.getElementById("conn-dot"),
  connLabel: document.getElementById("conn-label"),
});

screens.initScreenManager({
  header: document.getElementById("screen-header"),
  backBtn: document.getElementById("back-btn"),
  headerTitle: document.getElementById("header-title"),
  content: document.getElementById("screen-content"),
});

function refreshStatusBar() {
  const settings = state.getSettings();
  setDeviceLabel(settings.handheldLabel);
  setJudgeLabel(settings.judgeName);
}
refreshStatusBar();
setInterval(refreshStatusBar, 5000);

sync.init();
screens.push(buildHome, null, "Home");
