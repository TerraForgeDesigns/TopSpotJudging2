// REVIEW — mirrors ui/screens/review_screen.cpp: summary of everything
// entered, Confirm enqueues the car (matching judging::finish()'s wire
// shape exactly, per PROTOCOL.md's submissions[] fields) and triggers
// trigger (a) — sync immediately, ignoring backoff.
import * as screens from "../screens.js";
import * as state from "../state.js";
import * as sync from "../sync.js";
import { showToast } from "../toast.js";

function row(content, label, value) {
  const el = document.createElement("div");
  el.style.display = "flex";
  el.style.justifyContent = "space-between";
  const l = document.createElement("span");
  l.className = "text-secondary";
  l.textContent = label;
  const v = document.createElement("span");
  v.textContent = value || "Not entered";
  el.append(l, v);
  content.appendChild(el);
}

export function build(content) {
  const car = state.getDraft();
  const showInfo = state.getShowInfo();

  const detailsCard = document.createElement("div");
  detailsCard.className = "card";
  const header = document.createElement("div");
  header.style.fontFamily = "'Archivo', sans-serif";
  header.style.fontWeight = "700";
  header.style.fontSize = "23px";
  header.textContent = car.entryNumber;
  detailsCard.appendChild(header);
  row(detailsCard, "Participant", car.participant);
  row(detailsCard, "Year", car.year);
  row(detailsCard, "Make", car.make);
  row(detailsCard, "Model", car.model);
  row(detailsCard, "Vehicle Type", car.vehicleType);
  content.appendChild(detailsCard);

  const scoresCard = document.createElement("div");
  scoresCard.className = "card";
  const scoresHeader = document.createElement("div");
  scoresHeader.style.fontWeight = "600";
  scoresHeader.textContent = "Scores";
  scoresCard.appendChild(scoresHeader);
  let total = 0;
  for (const cat of showInfo.categories) {
    const s = car.scores.find((s) => s.categoryId === cat.id);
    const points = s ? s.points : -1;
    total += points >= 0 ? points : 0;
    row(scoresCard, cat.name, points >= 0 ? String(points) : "-");
  }
  if (showInfo.overallImpressionEnabled) {
    row(scoresCard, "Overall Impression", car.hasOverallImpression ? String(car.overallImpression) : "-");
  }
  row(scoresCard, "Total", `${total} / ${showInfo.maxScore}`);
  content.appendChild(scoresCard);

  const nomCard = document.createElement("div");
  nomCard.className = "card";
  const nomHeader = document.createElement("div");
  nomHeader.style.fontWeight = "600";
  nomHeader.textContent = "Award Nominations";
  nomCard.appendChild(nomHeader);
  if (car.nominations.length === 0) {
    const none = document.createElement("div");
    none.className = "text-secondary";
    none.textContent = "None";
    nomCard.appendChild(none);
  } else {
    for (const id of car.nominations) {
      const award = showInfo.judgeChosenAwards.find((a) => a.id === id);
      if (award) {
        const line = document.createElement("div");
        line.textContent = award.name;
        nomCard.appendChild(line);
      }
    }
  }
  content.appendChild(nomCard);

  const confirmBtn = document.createElement("button");
  confirmBtn.className = "btn btn-primary";
  confirmBtn.style.width = "320px";
  confirmBtn.style.marginTop = "16px";
  confirmBtn.textContent = "Confirm";
  confirmBtn.disabled = !(car.carPhotoSaved && car.sheetPhotoSaved);
  confirmBtn.addEventListener("click", () => {
    const settings = state.getSettings();
    const queuedCar = {
      entry_number: car.entryNumber,
      judge_name: settings.judgeName,
      closed_at_uptime_ms: Date.now(),
      participant: car.participant,
      year: car.year,
      make: car.make,
      model: car.model,
      vehicle_type: car.vehicleType,
      make_manually_entered: car.makeManuallyEntered,
      model_manually_entered: car.modelManuallyEntered,
      score_range_max: car.scoreRangeMax,
      scores: car.scores.map((s) => ({ category_id: s.categoryId, points: s.points })),
      overall_impression: car.hasOverallImpression ? car.overallImpression : null,
      nominations: car.nominations,
    };
    state.enqueueCar(queuedCar);
    state.clearDraft();
    sync.requestNow(); // trigger (a): a car was just finished — try immediately, ignoring backoff

    showToast(`Car ${car.entryNumber} is locked in and queued to send to Home Base next time this device connects.`, "success", 4000);
    screens.popToRoot();
  });
  content.appendChild(confirmBtn);
}

export function buildReviewScreen(content) {
  return build(content);
}
