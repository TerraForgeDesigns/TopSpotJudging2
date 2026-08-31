// MODEL SELECTOR — mirrors ui/screens/model_selector_screen.cpp: scoped
// to the current draft's Make, no search box under the 20-model
// threshold (task item 5).
import { buildVehicleSelector, findModels, MODEL_SEARCH_THRESHOLD } from "../vehicle_selector.js";
import * as screens from "../screens.js";
import * as state from "../state.js";

export async function build(content) {
  const car = state.getDraft();
  const probe = await findModels(car.make, "");

  buildVehicleSelector(content, {
    showSearchBox: probe.total >= MODEL_SEARCH_THRESHOLD,
    searchPlaceholder: "Search Models",
    manualPlaceholder: "Model",
    queryFn: (query) => findModels(car.make, query),
    recentsFn: () => state.getRecentModels(car.make),
  }).then(({ name, manuallyEntered }) => {
    const current = state.getDraft();
    current.model = name;
    current.modelManuallyEntered = manuallyEntered;
    state.recordModelUsed(current.make, name);
    state.saveDraft(current);
    screens.pop();
  });
}

export function buildModelSelectorScreen(content) {
  return build(content);
}
