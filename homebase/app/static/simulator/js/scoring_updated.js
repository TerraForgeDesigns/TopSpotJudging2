// The exact full-screen notice from ui/screens/scoring_updated_screen.cpp
// — same title, same copy (with the real new range substituted), same
// single Continue action, same trigger rule: only shown when a range
// change is seen AFTER a config was already applied once before (never
// on the very first sync of the day — see sync.js). No "range/scale/
// convert/revision/recalculate" anywhere, per LANGUAGE.md.
export function showScoringUpdated(newRangeMax) {
  return new Promise((resolve) => {
    const frame = document.querySelector(".device-frame");
    const overlay = document.createElement("div");
    overlay.className = "scoring-updated";

    const title = document.createElement("div");
    title.style.fontFamily = "'Archivo', sans-serif";
    title.style.fontWeight = "700";
    title.style.fontSize = "23px";
    title.textContent = "Scoring Updated";

    const body = document.createElement("div");
    body.className = "body-text";
    body.textContent =
      `This show now uses scores from 1 to ${newRangeMax} because more cars were added.\n\n` +
      "Scores you already sent have been adjusted automatically.";

    const continueBtn = document.createElement("button");
    continueBtn.className = "btn btn-primary";
    continueBtn.style.width = "320px";
    continueBtn.textContent = "Continue";
    continueBtn.addEventListener("click", () => {
      overlay.remove();
      resolve();
    });

    overlay.append(title, body, continueBtn);
    frame.appendChild(overlay);
  });
}
