// On-screen text keyboard overlay — mirrors
// ui/components/text_keyboard.cpp exactly: lowercase-only QWERTY, no
// shift row (ASCII-only, matches the firmware's embedded font glyph
// range), Space + Delete as the two wide keys, Cancel leaves the
// previous value untouched, Done accepts. This is the ONLY way text
// gets typed by default — see the physical-keyboard opt-in note below,
// which is the task's own explicit requirement: on-screen is the
// default path, physical keyboard is an explicit, off-by-default
// convenience, never a silent bypass.
import { getSettings } from "./state.js";

const ROWS = [
  ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
  ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
  ["z", "x", "c", "v", "b", "n", "m"],
];

// Resolves with { text, accepted }. `initialText` is what Cancel returns.
export function textKeyboardOverlay(initialText, placeholder, maxLen) {
  return new Promise((resolve) => {
    const frame = document.querySelector(".device-frame");
    const overlay = document.createElement("div");
    overlay.className = "key-overlay";

    let value = initialText || "";

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

    const grid = document.createElement("div");
    grid.className = "keyboard-grid";

    function render() {
      textarea.textContent = value || placeholder || "";
      textarea.style.color = value ? "" : "var(--text-secondary)";
    }

    function applyKey(key) {
      if (key === "Delete") {
        value = value.slice(0, -1);
      } else if (key === "Space") {
        if (maxLen && value.length >= maxLen) return;
        value += " ";
      } else {
        if (maxLen && value.length >= maxLen) return;
        value += key;
      }
      render();
    }

    for (const row of ROWS) {
      const rowEl = document.createElement("div");
      rowEl.className = "key-row";
      for (const k of row) {
        const btn = document.createElement("button");
        btn.className = "key";
        btn.textContent = k;
        btn.addEventListener("click", () => applyKey(k));
        rowEl.appendChild(btn);
      }
      grid.appendChild(rowEl);
    }
    const bottomRow = document.createElement("div");
    bottomRow.className = "key-row";
    const spaceBtn = document.createElement("button");
    spaceBtn.className = "key key-wide";
    spaceBtn.textContent = "Space";
    spaceBtn.addEventListener("click", () => applyKey("Space"));
    const deleteBtn = document.createElement("button");
    deleteBtn.className = "key key-wide";
    deleteBtn.textContent = "Delete";
    deleteBtn.addEventListener("click", () => applyKey("Delete"));
    bottomRow.append(spaceBtn, deleteBtn);
    grid.appendChild(bottomRow);

    function finish(accepted) {
      document.removeEventListener("keydown", onPhysicalKey);
      overlay.remove();
      resolve({ text: accepted ? value : initialText || "", accepted });
    }
    cancelBtn.addEventListener("click", () => finish(false));
    doneBtn.addEventListener("click", () => finish(true));

    // Physical keyboard — explicit opt-in only (Settings, default off).
    // Routes through the exact same applyKey()/finish() the on-screen
    // keys use, so behavior is identical either way — see this file's
    // header comment.
    function onPhysicalKey(e) {
      if (e.key === "Enter") return finish(true);
      if (e.key === "Escape") return finish(false);
      if (e.key === "Backspace") return applyKey("Delete");
      if (e.key === " ") return applyKey("Space");
      if (e.key.length === 1 && /[a-z]/i.test(e.key)) return applyKey(e.key.toLowerCase());
    }
    if (getSettings().usePhysicalKeyboard) {
      document.addEventListener("keydown", onPhysicalKey);
    }

    overlay.append(topRow, grid);
    frame.appendChild(overlay);
    render();
  });
}
