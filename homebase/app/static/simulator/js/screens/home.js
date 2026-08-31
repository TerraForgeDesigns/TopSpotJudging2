// HOME — mirrors ui/screens/home_screen.cpp: show progress, queue count,
// Update Now, Judge a Car, Settings.
import * as screens from "../screens.js";
import * as state from "../state.js";
import * as sync from "../sync.js";
import { buildEnterCarScreen } from "./enter_car.js";
import { buildSettingsScreen } from "./settings.js";

export function build(content) {
  const syncState = state.getSyncState();
  const queueCount = state.getQueue().length;

  const progressCard = document.createElement("div");
  progressCard.className = "card";
  progressCard.style.width = "480px";
  progressCard.style.margin = "0 auto";

  const label = document.createElement("div");
  label.className = "text-secondary";
  label.style.fontWeight = "500";
  label.style.fontSize = "15px";
  label.textContent = "SHOW PROGRESS";

  const big = document.createElement("div");
  big.style.fontFamily = "'Archivo', sans-serif";
  big.style.fontWeight = "800";
  big.style.fontSize = "36px";
  big.textContent = syncState.totalCars > 0 ? `${syncState.judgedCars} of ${syncState.totalCars} judged` : "Not updated yet";

  progressCard.append(label, big);

  if (syncState.flaggedConflictCars > 0) {
    const conflict = document.createElement("div");
    conflict.className = "text-secondary";
    conflict.textContent = `${syncState.flaggedConflictCars} flagged for the host to resolve`;
    progressCard.appendChild(conflict);
  }

  content.appendChild(progressCard);

  if (queueCount > 0) {
    const queueLine = document.createElement("div");
    queueLine.style.textAlign = "center";
    queueLine.className = "text-secondary";
    queueLine.textContent = queueCount === 1 ? "1 car waiting to send" : `${queueCount} cars waiting to send`;
    content.appendChild(queueLine);
  }

  const everSynced = syncState.lastSuccessfulUpdateEpochSeconds > 0;
  const ageSeconds = everSynced ? Math.floor(Date.now() / 1000) - syncState.lastSuccessfulUpdateEpochSeconds : 0;
  if (!everSynced || ageSeconds > 20 * 60) {
    const banner = document.createElement("div");
    banner.className = "banner banner-critical";
    banner.textContent = "Your scores are saved on this device. Walk toward Home Base to send them.";
    content.appendChild(banner);
  }

  const spacer = document.createElement("div");
  spacer.style.flex = "1";
  content.appendChild(spacer);

  const judgeBtn = document.createElement("button");
  judgeBtn.className = "btn btn-primary";
  judgeBtn.style.width = "320px";
  judgeBtn.style.alignSelf = "center";
  judgeBtn.textContent = "Judge a Car";
  judgeBtn.addEventListener("click", () => screens.push(buildEnterCarScreen, null, "Enter Car"));

  const updateBtn = document.createElement("button");
  updateBtn.className = "btn btn-secondary";
  updateBtn.style.width = "320px";
  updateBtn.style.alignSelf = "center";
  updateBtn.textContent = "Update Now";
  updateBtn.addEventListener("click", () => sync.requestNow());

  const settingsBtn = document.createElement("button");
  settingsBtn.className = "btn btn-secondary";
  settingsBtn.style.width = "320px";
  settingsBtn.style.alignSelf = "center";
  settingsBtn.textContent = "Settings";
  settingsBtn.addEventListener("click", () => screens.push(buildSettingsScreen, null, "Settings"));

  content.append(judgeBtn, updateBtn, settingsBtn);
}
