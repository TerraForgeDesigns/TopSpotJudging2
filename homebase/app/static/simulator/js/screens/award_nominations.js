// AWARD NOMINATIONS — mirrors ui/screens/award_nominations_screen.cpp:
// the checklist of judge-chosen awards, none required.
import { buildChecklistRow } from "../nominations.js";
import * as screens from "../screens.js";
import * as state from "../state.js";
import { buildPhotosScreen } from "./photos.js";

export function build(content) {
  const car = state.getDraft();
  const showInfo = state.getShowInfo();

  const hint = document.createElement("div");
  hint.className = "text-secondary";
  hint.textContent = "Tap any awards this car should be considered for. None are required.";
  content.appendChild(hint);

  if (showInfo.judgeChosenAwards.length === 0) {
    const empty = document.createElement("div");
    empty.className = "text-secondary";
    empty.textContent = "This show has no judge-chosen awards to nominate for.";
    content.appendChild(empty);
  } else {
    const list = document.createElement("div");
    list.className = "card";
    for (const award of showInfo.judgeChosenAwards) {
      buildChecklistRow(list, award.name, car.nominations.includes(award.id), (checked) => {
        if (checked) {
          if (!car.nominations.includes(award.id)) car.nominations.push(award.id);
        } else {
          car.nominations = car.nominations.filter((id) => id !== award.id);
        }
        state.saveDraft(car);
      });
    }
    content.appendChild(list);
  }

  const continueBtn = document.createElement("button");
  continueBtn.className = "btn btn-primary";
  continueBtn.style.width = "320px";
  continueBtn.style.marginTop = "16px";
  continueBtn.textContent = "Continue to Photos";
  continueBtn.addEventListener("click", () => {
    car.furthestStep = "photos";
    state.saveDraft(car);
    screens.push(buildPhotosScreen, null, "Photos");
  });
  content.appendChild(continueBtn);
}

export function buildAwardNominationsScreen(content) {
  return build(content);
}
