// Persistent status bar — mirrors ui/components/status_bar.cpp's public
// API and exact label wording ("UP TO DATE" / "UPDATING" / "NOT
// CONNECTED" — LANGUAGE.md's cross-platform-identical vocabulary).
let elDeviceLabel, elJudgeLabel, elConnDot, elConnLabel;

export function initStatusBar({ deviceLabel, judgeLabel, connDot, connLabel }) {
  elDeviceLabel = deviceLabel;
  elJudgeLabel = judgeLabel;
  elConnDot = connDot;
  elConnLabel = connLabel;
  setConnState("not_connected");
}

export function setDeviceLabel(text) {
  elDeviceLabel.textContent = text || "Unnamed";
}
export function setJudgeLabel(text) {
  elJudgeLabel.textContent = text || "";
}

export function setConnState(state) {
  elConnDot.classList.remove("good", "pending", "bad");
  switch (state) {
    case "up_to_date":
      elConnDot.classList.add("good");
      elConnLabel.textContent = "UP TO DATE";
      break;
    case "updating":
      elConnDot.classList.add("pending");
      elConnLabel.textContent = "UPDATING";
      break;
    default:
      elConnDot.classList.add("bad");
      elConnLabel.textContent = "NOT CONNECTED";
      break;
  }
}
