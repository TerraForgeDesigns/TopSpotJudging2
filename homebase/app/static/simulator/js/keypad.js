// On-screen numeric keypad — mirrors ui/components/numeric_keypad.cpp
// (digits + Clear + Delete, no punctuation) and
// ui/components/numeric_keypad_overlay.cpp (the modal variant with
// auto-accept-at-length + a validator, used for Year). Same
// physical-keyboard opt-in rule as keyboard.js — see that file's header
// comment.
import { getSettings } from "./state.js";

const KEYS = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "Clear", "0", "Delete"];

// Builds the plain keypad grid into `container`. `onKey(key)` is called
// for every tap: '0'-'9', 'C' (Clear), or 'B' (Backspace) — matching
// numeric_keypad.h's KeypadCallback contract exactly.
export function buildNumericKeypad(container, onKey) {
  const grid = document.createElement("div");
  grid.className = "keypad-grid";
  for (const k of KEYS) {
    const btn = document.createElement("button");
    btn.className = "key";
    btn.textContent = k;
    btn.addEventListener("click", () => {
      if (k === "Clear") onKey("C");
      else if (k === "Delete") onKey("B");
      else onKey(k);
    });
    grid.appendChild(btn);
  }
  container.appendChild(grid);
  return grid;
}

// Resolves with { text, accepted }, same contract as textKeyboardOverlay.
// `autoAcceptLen` > 0 auto-fires Done the instant that many digits are
// typed AND `validator` accepts them (Year: 4 digits, closes on a
// plausible year — see vehicle_details_screen.cpp's onEditYear).
export function numericKeypadOverlay(initialText, placeholder, maxLen, autoAcceptLen, validator, invalidHint) {
  return new Promise((resolve) => {
    const frame = document.querySelector(".device-frame");
    const overlay = document.createElement("div");
    overlay.className = "key-overlay";

    let value = "";

    const topRow = document.createElement("div");
    topRow.className = "key-top-row";
    const textarea = document.createElement("div");
    textarea.className = "key-textarea";
    const cancelBtn = document.createElement("button");
    cancelBtn.className = "btn btn-secondary";
    cancelBtn.textContent = "Cancel";
    cancelBtn.style.height = "64px";
    const doneBtn = document.createElement("button");
    doneBtn.className = "btn btn-primary";
    doneBtn.textContent = "Done";
    doneBtn.style.height = "64px";
    topRow.append(textarea, cancelBtn, doneBtn);

    const hint = document.createElement("div");
    hint.className = "key-hint";

    function isValid() {
      if (!value) return false;
      return validator ? validator(value) : true;
    }

    function render() {
      textarea.textContent = value || placeholder || "";
      textarea.style.color = value ? "" : "var(--text-secondary)";
      const valid = isValid();
      doneBtn.disabled = !valid;
      hint.textContent = !valid && value && invalidHint ? invalidHint : "";
    }

    function finish(accepted) {
      document.removeEventListener("keydown", onPhysicalKey);
      overlay.remove();
      resolve({ text: accepted ? value : initialText || "", accepted });
    }

    function applyKey(key) {
      if (key === "C") value = "";
      else if (key === "B") value = value.slice(0, -1);
      else if (!maxLen || value.length < maxLen) value += key;
      render();
      if (autoAcceptLen > 0 && value.length === autoAcceptLen && isValid()) finish(true);
    }

    const keypadWrap = document.createElement("div");
    keypadWrap.style.flex = "1";
    keypadWrap.style.display = "flex";
    keypadWrap.style.alignItems = "center";
    keypadWrap.style.justifyContent = "center";
    buildNumericKeypad(keypadWrap, applyKey);

    cancelBtn.addEventListener("click", () => finish(false));
    doneBtn.addEventListener("click", () => {
      if (isValid()) finish(true);
    });

    function onPhysicalKey(e) {
      if (e.key === "Enter") return doneBtn.click();
      if (e.key === "Escape") return finish(false);
      if (e.key === "Backspace") return applyKey("B");
      if (/^[0-9]$/.test(e.key)) return applyKey(e.key);
    }
    if (getSettings().usePhysicalKeyboard) {
      document.addEventListener("keydown", onPhysicalKey);
    }

    overlay.append(topRow, hint, keypadWrap);
    frame.appendChild(overlay);
    render();
  });
}
