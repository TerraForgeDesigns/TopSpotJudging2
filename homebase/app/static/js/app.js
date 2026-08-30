// Small vanilla-JS helpers. No framework, no build step — see /README.md.

document.addEventListener("click", (event) => {
  const opener = event.target.closest("[data-modal-open]");
  if (opener) {
    const modal = document.getElementById(opener.dataset.modalOpen);
    if (modal) {
      // Generic confirm-modal population: a trigger like
      //   <button data-modal-open="delete-x-modal" data-fill-name="0142" data-fill-action="/cars/5/delete">
      // fills every [data-fill-target="name"] element's text (or a <form>'s
      // action attribute) inside the target modal. Lets one shared modal per
      // page serve every row without stamping out N hidden modals.
      for (const key in opener.dataset) {
        if (key === "modalOpen" || !key.startsWith("fill")) continue;
        const targetKey = key.slice(4, 5).toLowerCase() + key.slice(5);
        const value = opener.dataset[key];
        modal.querySelectorAll(`[data-fill-target="${targetKey}"]`).forEach((el) => {
          if (el.tagName === "FORM") {
            el.action = value;
          } else {
            el.textContent = value;
          }
        });
      }
      modal.hidden = false;
    }
  }

  const closer = event.target.closest("[data-modal-close]");
  if (closer) {
    const modal = closer.closest(".modal-overlay");
    if (modal) modal.hidden = true;
  }

  const dismiss = event.target.closest("[data-toast-dismiss]");
  if (dismiss) {
    const toast = dismiss.closest(".toast");
    if (toast) toast.remove();
  }
});

// Client-side roster filtering (see /cars) — 400 rows, filtered instantly
// with no server round trip. Each filterable table opts in with
// data-filter-table, filter inputs point at it with data-filter-for.
document.addEventListener("input", (event) => {
  const control = event.target.closest("[data-filter-for]");
  if (!control) return;
  const table = document.querySelector(`[data-filter-table="${control.dataset.filterFor}"]`);
  if (!table) return;
  applyTableFilters(table);
});

document.addEventListener("change", (event) => {
  const control = event.target.closest("[data-filter-for]");
  if (!control) return;
  const table = document.querySelector(`[data-filter-table="${control.dataset.filterFor}"]`);
  if (!table) return;
  applyTableFilters(table);
});

// Generic drag-to-reorder — used by the Create Show wizard for the
// tie-break category priority list and the Show Awards list. Native
// HTML5 drag-and-drop, no library (matches the existing project
// convention for reorderable lists — see DECISIONS.md).
//
// Markup contract:
//   <div data-drag-list data-drag-order-input="some-id">
//     <div data-drag-item data-drag-id="...">...</div>
//     ...
//   </div>
//   <input type="hidden" id="some-id" name="order" ...>
//
// On drop, the container's children are reordered in the DOM and the
// referenced hidden input's value becomes the new comma-separated
// data-drag-id order, then a "change" event fires on it — an htmx
// listener on that input (hx-trigger="change") is what actually
// persists the new order server-side. This file only ever touches the
// DOM and that one input; it has no opinion on what happens next.
let draggedItem = null;

document.addEventListener("dragstart", (event) => {
  const item = event.target.closest("[data-drag-item]");
  if (!item) return;
  draggedItem = item;
  item.classList.add("is-dragging");
});

document.addEventListener("dragend", (event) => {
  const item = event.target.closest("[data-drag-item]");
  if (item) item.classList.remove("is-dragging");
  draggedItem = null;
});

document.addEventListener("dragover", (event) => {
  const list = event.target.closest("[data-drag-list]");
  if (!list || !draggedItem) return;
  event.preventDefault();
  const after = dragAfterElement(list, event.clientY);
  if (after == null) {
    list.appendChild(draggedItem);
  } else {
    list.insertBefore(draggedItem, after);
  }
});

document.addEventListener("drop", (event) => {
  const list = event.target.closest("[data-drag-list]");
  if (!list || !draggedItem) return;
  event.preventDefault();
  const order = Array.from(list.querySelectorAll("[data-drag-item]")).map((el) => el.dataset.dragId);
  const input = document.getElementById(list.dataset.dragOrderInput);
  if (input) {
    input.value = order.join(",");
    input.dispatchEvent(new Event("change", { bubbles: true }));
  }
});

function dragAfterElement(list, y) {
  const items = [...list.querySelectorAll("[data-drag-item]:not(.is-dragging)")];
  return items.reduce(
    (closest, child) => {
      const box = child.getBoundingClientRect();
      const offset = y - box.top - box.height / 2;
      if (offset < 0 && offset > closest.offset) {
        return { offset, element: child };
      }
      return closest;
    },
    { offset: Number.NEGATIVE_INFINITY, element: null }
  ).element;
}

function applyTableFilters(table) {
  const scope = table.dataset.filterTable;
  const controls = document.querySelectorAll(`[data-filter-for="${scope}"]`);
  const filters = {};
  controls.forEach((el) => {
    const key = el.dataset.filterKey;
    const value = (el.value || "").trim().toLowerCase();
    if (key && value) filters[key] = value;
  });

  const rows = table.querySelectorAll("tbody tr[data-row]");
  let visibleCount = 0;
  rows.forEach((row) => {
    let visible = true;
    for (const key in filters) {
      const rowValue = (row.dataset[key] || "").toLowerCase();
      if (key === "status" || key === "class") {
        if (rowValue !== filters[key]) {
          visible = false;
          break;
        }
      } else if (!rowValue.includes(filters[key])) {
        visible = false;
        break;
      }
    }
    row.hidden = !visible;
    if (visible) visibleCount += 1;
  });

  const emptyRow = table.querySelector("tbody tr[data-empty-row]");
  if (emptyRow) emptyRow.hidden = visibleCount !== 0;
}
