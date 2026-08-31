// VEHICLE DETAILS — mirrors ui/screens/vehicle_details_screen.cpp:
// Participant/Vehicle Type via the text keyboard, Year via the
// no-punctuation numeric keypad overlay (auto-closes on a plausible
// 4-digit year), Make/Model via the selector screens.
import { textKeyboardOverlay } from "../keyboard.js";
import { numericKeypadOverlay } from "../keypad.js";
import * as screens from "../screens.js";
import * as state from "../state.js";
import { buildMakeSelectorScreen } from "./make_selector.js";
import { buildModelSelectorScreen } from "./model_selector.js";
import { buildJudgeCarScreen } from "./judge_car.js";

const MIN_VEHICLE_YEAR = 1885;
const FALLBACK_MAX_VEHICLE_YEAR = 2035;

function isValidYear(text) {
  if (text.length !== 4) return false;
  const year = parseInt(text, 10);
  const showInfo = state.getShowInfo();
  const maxYear = showInfo.eventYear > 0 ? showInfo.eventYear + 1 : FALLBACK_MAX_VEHICLE_YEAR;
  return year >= MIN_VEHICLE_YEAR && year <= maxYear;
}

function row(content, title, value, onClick) {
  const el = document.createElement("div");
  el.className = "list-row";
  const t = document.createElement("span");
  t.textContent = title;
  const v = document.createElement("span");
  v.className = "row-value";
  v.textContent = value || "Not entered yet";
  el.append(t, v);
  el.addEventListener("click", onClick);
  content.appendChild(el);
}

export function build(content) {
  const car = state.getDraft();

  if (car.notInRosterYet) {
    const banner = document.createElement("div");
    banner.className = "banner banner-info";
    banner.textContent = "This car number is not in the list yet. You can still judge it.";
    content.appendChild(banner);
  }

  row(content, "Participant", car.participant, async () => {
    const result = await textKeyboardOverlay(car.participant, "Participant", 79);
    if (result.accepted) {
      car.participant = result.text;
      state.saveDraft(car);
      screens.refresh();
    }
  });

  row(content, "Year", car.year, async () => {
    const result = await numericKeypadOverlay(car.year, "Year", 4, 4, isValidYear, "Not a plausible year for this show. Check the digits.");
    if (result.accepted) {
      car.year = result.text;
      state.saveDraft(car);
      screens.refresh();
    }
  });

  row(content, "Make", car.make, () => screens.push(buildMakeSelectorScreen, null, "Make"));
  row(content, "Model", car.model, () => screens.push(buildModelSelectorScreen, null, "Model"));

  row(content, "Vehicle Type", car.vehicleType, async () => {
    const result = await textKeyboardOverlay(car.vehicleType, "Vehicle Type", 31);
    if (result.accepted) {
      car.vehicleType = result.text;
      state.saveDraft(car);
      screens.refresh();
    }
  });

  const continueBtn = document.createElement("button");
  continueBtn.className = "btn btn-primary";
  continueBtn.style.width = "320px";
  continueBtn.style.marginTop = "16px";
  continueBtn.textContent = "Continue to Scoring";
  continueBtn.addEventListener("click", () => {
    car.furthestStep = "judging";
    state.saveDraft(car);
    screens.push(buildJudgeCarScreen, null, "Judge Car");
  });
  content.appendChild(continueBtn);
}

export function buildVehicleDetailsScreen(content) {
  return build(content);
}
