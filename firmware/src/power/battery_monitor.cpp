#include "battery_monitor.h"

#include "ui/components/toast.h"

namespace power::battery_monitor {

namespace {

enum class Tier { Normal, Low, Critical };

// Starts at Normal — a device that boots already below a threshold
// (unlikely in practice, since it'd need power to boot at all, but not
// impossible right after a partial charge) still gets its first warning
// on the very next check() rather than assuming it was already shown.
Tier g_lastWarnedTier = Tier::Normal;

constexpr int LOW_THRESHOLD_PCT = 20;
constexpr int CRITICAL_THRESHOLD_PCT = 10;

Tier tierFor(int percent) {
    if (percent < 0) return Tier::Normal;  // unknown is never a warning state
    if (percent <= CRITICAL_THRESHOLD_PCT) return Tier::Critical;
    if (percent <= LOW_THRESHOLD_PCT) return Tier::Low;
    return Tier::Normal;
}

}  // namespace

void check(int percent) {
    Tier tier = tierFor(percent);

    // Only warn on a genuine downward transition into a NEW lower tier —
    // never repeats every call, and a climb back above a threshold (e.g.
    // after charging) resets the tracker so a future drop warns again.
    if (tier == g_lastWarnedTier) return;
    g_lastWarnedTier = tier;

    switch (tier) {
        case Tier::Critical:
            // The task's exact wording — frames the REAL risk (unsynced
            // work on a dying device), not just the battery fact.
            ui::components::showToast("Battery low. Send your work to Home Base soon.",
                                       ui::components::ToastSeverity::Error, 6000);
            break;
        case Tier::Low:
            ui::components::showToast("Battery getting low.", ui::components::ToastSeverity::Error, 4000);
            break;
        case Tier::Normal:
            break;  // climbing back to Normal is silent — nothing to warn about
    }
}

}  // namespace power::battery_monitor
