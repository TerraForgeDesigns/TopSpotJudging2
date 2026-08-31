// Stack-based screen navigation. Requirement: screens create and destroy
// cleanly — with a 768KB resident framebuffer and 4MB of flash, an
// accumulation of hidden-but-never-freed screen trees is exactly the kind
// of leak that surfaces as an out-of-memory crash hours into a real show,
// not at bring-up. So only ONE screen's widget tree ever exists at a time:
// pushing a new screen destroys the current one's tree immediately (not
// just hides it); popping reconstructs the previous screen fresh from a
// lightweight factory recorded on the stack, not from a kept-alive object.
// A screen that needs to remember something across being popped-to later
// (e.g. "which car is this score for") owns that as a `void*` context
// argument passed back into its own factory — the manager itself is
// stateless about screen internals.
#pragma once

#include <lvgl.h>

namespace ui {

// One screen. Implementations live in ui/screens/. build() must create
// every widget it needs as a child of `content` (never keep a pointer to
// `content` beyond build() — it's destroyed on pop/replace); teardown()
// is the place to release anything NOT owned by the LVGL object tree
// (a lv_timer_t this screen created, a heap buffer, ...) — called right
// before the manager deletes the tree itself.
class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* title() const = 0;
    virtual void build(lv_obj_t* content) = 0;
    virtual void teardown() {}
    virtual void onShow() {}
};

using ScreenFactory = Screen* (*)(void* arg);

namespace screen_manager {

// Creates the persistent status bar (LVGL top layer — survives every
// screen change untouched, see ui/components/status_bar.h) and the
// header/content containers. Call once, after ui::lvglInit() and
// ui::theme::init().
void init();

// Pushes a new screen built by `factory(arg)`, tearing down and freeing
// the current screen's tree first. `title` overrides screen->title() in
// the header if non-null (rare — most screens should just implement
// title()). The root screen (stack depth 1) shows no Back button.
void push(ScreenFactory factory, void* arg = nullptr, const char* title = nullptr);

// Tears down the current screen and rebuilds the one beneath it from its
// recorded factory. No-op if already at the root (depth 1) — there is
// nothing to go back to.
void pop();

// Pops all the way back to the root screen in one step (e.g. after
// finishing a multi-step flow) — never leaves a dangling depth on the
// stack from skipped intermediate pops.
void popToRoot();

// Tears down and rebuilds the CURRENT screen from its own factory,
// staying at the same stack depth — for a screen that changed something
// it displays (e.g. Settings, after editing a field) and wants an easy
// full re-render rather than hand-updating individual widgets.
void refresh();

int depth();

}  // namespace screen_manager
}  // namespace ui
