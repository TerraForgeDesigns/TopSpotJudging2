// Award nominations checklist — mirrors
// ui/components/checklist_row.cpp: a checkbox + label row, explicit
// yes/no toggle state, not a single-select.
export function buildChecklistRow(container, label, initialChecked, onChange) {
  const row = document.createElement("label");
  row.className = "checklist-row";
  const box = document.createElement("input");
  box.type = "checkbox";
  box.checked = initialChecked;
  box.addEventListener("change", () => onChange(box.checked));
  const text = document.createElement("span");
  text.textContent = label;
  row.append(box, text);
  container.appendChild(row);
  return row;
}
