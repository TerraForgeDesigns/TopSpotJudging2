// MAKE SELECTOR — mirrors ui/screens/make_selector_screen.cpp, including
// the Make -> Model auto-advance rule: Model blank, or no longer valid
// under the newly-chosen Make, advances straight into the Model
// selector (pop + push, same stack depth either way); Model still valid
// (including "Make didn't change") returns to Vehicle Details.
import { buildVehicleSelector, findMakes, modelBelongsToMake } from "../vehicle_selector.js";
import * as screens from "../screens.js";
import * as state from "../state.js";
import { buildModelSelectorScreen } from "./model_selector.js";

export function build(content) {
  buildVehicleSelector(content, {
    showSearchBox: true,
    searchPlaceholder: "Search Makes",
    manualPlaceholder: "Make",
    queryFn: (query) => findMakes(query),
    recentsFn: () => state.getRecentMakes(),
  }).then(async ({ name, manuallyEntered }) => {
    const car = state.getDraft();
    const wasBlank = !car.model;
    const makeChanged = (car.make || "").toLowerCase() !== name.toLowerCase();

    car.make = name;
    car.makeManuallyEntered = manuallyEntered;
    state.recordMakeUsed(name);

    let modelCleared = false;
    if (makeChanged && car.model && !(await modelBelongsToMake(name, car.model))) {
      car.model = "";
      car.modelManuallyEntered = false;
      modelCleared = true;
    }
    state.saveDraft(car);

    screens.pop();
    if (wasBlank || modelCleared) screens.push(buildModelSelectorScreen, null, "Model");
  });
}

export function buildMakeSelectorScreen(content) {
  return build(content);
}
