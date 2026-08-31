// ENTER CAR — mirrors ui/screens/enter_car_screen.cpp: the plain
// on-screen numeric keypad (embedded directly, not an overlay — matches
// firmware exactly, this screen IS the keypad), resumes an existing
// draft at its furthest step, refuses a second attempt at an entry this
// device already finished.
import { buildNumericKeypad } from "../keypad.js";
import * as screens from "../screens.js";
import * as state from "../state.js";
import { showToast } from "../toast.js";
import { buildVehicleDetailsScreen } from "./vehicle_details.js";
import { buildJudgeCarScreen } from "./judge_car.js";
import { buildAwardNominationsScreen } from "./award_nominations.js";
import { buildPhotosScreen } from "./photos.js";
import { buildReviewScreen } from "./review.js";

const STEP_SCREENS = {
  vehicle_details: buildVehicleDetailsScreen,
  judging: buildJudgeCarScreen,
  nominations: buildAwardNominationsScreen,
  photos: buildPhotosScreen,
  review: buildReviewScreen,
};

export function build(content) {
  let typed = "";

  const display = document.createElement("div");
  display.style.fontFamily = "'Archivo', sans-serif";
  display.style.fontWeight = "800";
  display.style.fontSize = "44px";
  display.style.textAlign = "center";
  display.style.margin = "16px 0";

  const continueBtn = document.createElement("button");
  continueBtn.className = "btn btn-primary";
  continueBtn.style.width = "320px";
  continueBtn.style.alignSelf = "center";
  continueBtn.textContent = "Continue";
  continueBtn.disabled = true;

  function render() {
    display.textContent = typed || "Type an entry number";
    display.style.color = typed ? "" : "var(--text-secondary)";
    continueBtn.disabled = typed.length === 0;
  }

  const keypadWrap = document.createElement("div");
  keypadWrap.style.display = "flex";
  keypadWrap.style.justifyContent = "center";
  buildNumericKeypad(keypadWrap, (key) => {
    if (key === "C") typed = "";
    else if (key === "B") typed = typed.slice(0, -1);
    else if (typed.length < 6) typed += key;
    render();
  });

  continueBtn.addEventListener("click", () => {
    const draft = state.getDraft();
    const resuming = draft && draft.entryNumber === typed;
    if (!resuming && state.isEntryQueued(typed)) {
      showToast("This car has already been judged on this device.", "error", 3500);
      return;
    }

    const showInfo = state.getShowInfo();
    let car = draft && draft.entryNumber === typed ? draft : null;
    if (!car) {
      car = {
        entryNumber: typed,
        furthestStep: "vehicle_details",
        notInRosterYet: false,
        participant: "",
        year: "",
        make: "",
        model: "",
        vehicleType: "",
        makeManuallyEntered: false,
        modelManuallyEntered: false,
        scoreRangeMax: showInfo.scoreRangeMax,
        scores: [],
        hasOverallImpression: false,
        overallImpression: 0,
        nominations: [],
        carPhotoSaved: false,
        sheetPhotoSaved: false,
      };
      const entry = state.findEntry(typed);
      if (entry) {
        car.participant = entry.participant;
        car.year = entry.year;
        car.make = entry.make;
        car.model = entry.model;
        car.vehicleType = entry.vehicle_type;
      } else {
        car.notInRosterYet = true;
      }
      state.saveDraft(car);
    }

    const nextScreen = STEP_SCREENS[car.furthestStep] || buildVehicleDetailsScreen;
    screens.push(nextScreen, null, screenTitleFor(car.furthestStep));
  });

  function screenTitleFor(step) {
    return { vehicle_details: "Vehicle Details", judging: "Judge Car", nominations: "Award Nominations", photos: "Photos", review: "Review" }[step] || "Vehicle Details";
  }

  content.append(display, keypadWrap, continueBtn);
  render();
}

export function buildEnterCarScreen(content) {
  return build(content);
}
