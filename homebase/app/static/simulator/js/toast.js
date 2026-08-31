// Bottom-anchored, auto-dismissing toast — mirrors
// ui/components/toast.h's showToast(text, severity, durationMs).
export function showToast(text, severity = "success", durationMs = 2500) {
  const frame = document.querySelector(".device-frame");
  if (!frame) return;
  const el = document.createElement("div");
  el.className = `toast ${severity === "error" ? "error" : "success"}`;
  el.textContent = text;
  frame.appendChild(el);
  setTimeout(() => el.remove(), durationMs);
}
