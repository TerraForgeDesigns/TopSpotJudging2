// Both scoring layouts — mirrors ui/components/score_row.cpp (tap-once
// row, used for the 1-5 and 1-10 ranges) and ui/components/score_grid.cpp
// (a fixed 5x5 grid, ALWAYS 1-25 — score_grid.cpp takes no range
// parameter at all, same as the real component). Which one a screen uses
// is the same threshold judge_car_screen.cpp uses: range <= 10 -> row,
// otherwise -> grid.

export function buildScoreRow(container, max, currentValue, onSelect) {
  const row = document.createElement("div");
  row.className = "score-row";
  for (let i = 1; i <= max; i++) {
    const cell = document.createElement("button");
    cell.className = "score-cell" + (i === currentValue ? " selected" : "");
    cell.textContent = String(i);
    cell.addEventListener("click", () => onSelect(i));
    row.appendChild(cell);
  }
  container.appendChild(row);
  return row;
}

export function buildScoreGrid(container, currentValue, onSelect) {
  const grid = document.createElement("div");
  grid.className = "score-grid5";
  for (let i = 1; i <= 25; i++) {
    const cell = document.createElement("button");
    cell.className = "score-cell" + (i === currentValue ? " selected" : "");
    cell.textContent = String(i);
    cell.addEventListener("click", () => onSelect(i));
    grid.appendChild(cell);
  }
  container.appendChild(grid);
  return grid;
}
