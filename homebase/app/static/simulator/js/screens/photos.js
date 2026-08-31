// PHOTOS — no real camera exists in a browser the way
// camera::captureToFile() does; a "Simulate Photo Capture" action stands
// in per slot. NOT a fidelity target itself (the task's item 2 doesn't
// list it), but the same "both required before Review" gate
// photos_screen.cpp enforces is still modeled, so Review/Confirm's real
// behavior is genuinely exercised.
import * as screens from "../screens.js";
import * as state from "../state.js";
import { showToast } from "../toast.js";
import { buildReviewScreen } from "./review.js";

function buildSlot(content, title, saved, onCapture) {
  const wrap = document.createElement("div");
  wrap.className = "card";
  wrap.style.flex = "1";
  const titleEl = document.createElement("div");
  titleEl.style.fontFamily = "'IBM Plex Sans', sans-serif";
  titleEl.style.fontWeight = "600";
  titleEl.style.fontSize = "18px";
  titleEl.textContent = title;

  const box = document.createElement("div");
  box.className = "raised-surface";
  box.style.height = "140px";
  box.style.display = "flex";
  box.style.alignItems = "center";
  box.style.justifyContent = "center";
  box.className += " text-secondary";
  box.textContent = saved ? "Photo captured" : "No photo yet";

  const btn = document.createElement("button");
  btn.className = "btn btn-secondary";
  btn.textContent = saved ? "Retake" : "Simulate Photo Capture";
  btn.addEventListener("click", onCapture);

  wrap.append(titleEl, box, btn);
  content.appendChild(wrap);
}

export function build(content) {
  const car = state.getDraft();

  const row = document.createElement("div");
  row.style.display = "flex";
  row.style.gap = "12px";

  buildSlot(row, "Vehicle Photo", car.carPhotoSaved, () => {
    car.carPhotoSaved = true;
    state.saveDraft(car);
    screens.refresh();
  });
  buildSlot(row, "Judge Sheet Photo", car.sheetPhotoSaved, () => {
    car.sheetPhotoSaved = true;
    state.saveDraft(car);
    screens.refresh();
  });
  content.appendChild(row);

  const continueBtn = document.createElement("button");
  continueBtn.className = "btn btn-primary";
  continueBtn.style.width = "320px";
  continueBtn.style.marginTop = "16px";
  continueBtn.textContent = "Continue to Review";
  continueBtn.addEventListener("click", () => {
    if (!car.carPhotoSaved || !car.sheetPhotoSaved) {
      showToast("Both photos are required before continuing.", "error", 3000);
      return;
    }
    car.furthestStep = "review";
    state.saveDraft(car);
    screens.push(buildReviewScreen, null, "Review");
  });
  content.appendChild(continueBtn);
}

export function buildPhotosScreen(content) {
  return build(content);
}
