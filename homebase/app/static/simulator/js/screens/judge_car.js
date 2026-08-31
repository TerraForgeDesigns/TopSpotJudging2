// JUDGE CAR — mirrors ui/screens/judge_car_screen.cpp: row layout for
// range <=10, the fixed 5x5 grid for 25 — same threshold, same rule that
// a draft already in progress keeps ITS OWN range even if the live show
// has since escalated (only a genuinely fresh draft adopts the current
// range — see DECISIONS.md's F5 entry on this exact firmware bug/fix).
import { buildScoreRow, buildScoreGrid } from "../scoring.js";
import * as screens from "../screens.js";
import * as state from "../state.js";
import { showToast } from "../toast.js";
import { buildAwardNominationsScreen } from "./award_nominations.js";

const OVERALL_IMPRESSION_ID = -1;

function getScore(car, categoryId) {
  if (categoryId === OVERALL_IMPRESSION_ID) return car.hasOverallImpression ? car.overallImpression : -1;
  const s = car.scores.find((s) => s.categoryId === categoryId);
  return s ? s.points : -1;
}
function setScore(car, categoryId, points) {
  if (categoryId === OVERALL_IMPRESSION_ID) {
    car.hasOverallImpression = true;
    car.overallImpression = points;
  } else {
    const existing = car.scores.find((s) => s.categoryId === categoryId);
    if (existing) existing.points = points;
    else car.scores.push({ categoryId, points });
  }
  state.saveDraft(car);
}

export function build(content) {
  const car = state.getDraft();
  const showInfo = state.getShowInfo();

  // Adopt the show's current range only while genuinely untouched — see
  // this file's header comment.
  if (car.scores.length === 0 && !car.hasOverallImpression) {
    car.scoreRangeMax = showInfo.scoreRangeMax;
    state.saveDraft(car);
  }

  const useGrid = car.scoreRangeMax > 10;

  function buildCategory(cat) {
    const wrap = document.createElement("div");
    const label = document.createElement("div");
    label.style.fontFamily = "'Archivo', sans-serif";
    label.style.fontWeight = "600";
    label.style.fontSize = "18px";
    label.textContent = cat.name;
    wrap.appendChild(label);

    const rerender = () => {
      screens.refresh();
    };
    if (useGrid) {
      buildScoreGrid(wrap, getScore(car, cat.id), (v) => {
        setScore(car, cat.id, v);
        rerender();
      });
    } else {
      buildScoreRow(wrap, car.scoreRangeMax, getScore(car, cat.id), (v) => {
        setScore(car, cat.id, v);
        rerender();
      });
    }
    return wrap;
  }

  const scrollArea = document.createElement("div");
  scrollArea.style.display = "flex";
  scrollArea.style.flexDirection = "column";
  scrollArea.style.gap = "12px";
  scrollArea.style.flex = "1";
  scrollArea.style.overflowY = "auto";

  for (const cat of showInfo.categories) {
    scrollArea.appendChild(buildCategory(cat));
  }
  if (showInfo.overallImpressionEnabled) {
    const hint = document.createElement("div");
    hint.className = "text-secondary";
    hint.style.fontSize = "13px";
    hint.textContent = "Used only to settle ties. Does not count toward the score.";
    scrollArea.appendChild(hint);
    scrollArea.appendChild(buildCategory({ id: OVERALL_IMPRESSION_ID, name: "Overall Impression" }));
  }
  content.appendChild(scrollArea);

  const total = car.scores.reduce((sum, s) => sum + s.points, 0);
  const bar = document.createElement("div");
  bar.style.display = "flex";
  bar.style.justifyContent = "space-between";
  bar.style.alignItems = "center";
  bar.style.padding = "8px 0";
  const totalLabel = document.createElement("div");
  totalLabel.style.fontFamily = "'Archivo', sans-serif";
  totalLabel.style.fontWeight = "700";
  totalLabel.style.fontSize = "26px";
  totalLabel.textContent = `${total} / ${showInfo.maxScore}`;

  const continueBtn = document.createElement("button");
  continueBtn.className = "btn btn-primary";
  continueBtn.textContent = "Continue";
  continueBtn.addEventListener("click", () => {
    for (const cat of showInfo.categories) {
      if (getScore(car, cat.id) < 0) {
        showToast(`${cat.name} has not been scored. Choose a ${cat.name} score before continuing.`, "error", 3500);
        return;
      }
    }
    if (showInfo.overallImpressionEnabled && getScore(car, OVERALL_IMPRESSION_ID) < 0) {
      showToast("Give an Overall Impression score before continuing.", "error", 3500);
      return;
    }
    car.furthestStep = "nominations";
    state.saveDraft(car);
    screens.push(buildAwardNominationsScreen, null, "Award Nominations");
  });

  bar.append(totalLabel, continueBtn);
  content.appendChild(bar);
}

export function buildJudgeCarScreen(content) {
  return build(content);
}
