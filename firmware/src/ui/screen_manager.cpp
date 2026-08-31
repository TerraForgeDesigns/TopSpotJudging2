#include "screen_manager.h"

#include "components/status_bar.h"
#include "fonts/fonts.h"
#include "theme.h"

namespace ui::screen_manager {

namespace {

constexpr int MAX_DEPTH = 8;  // realistic worst case for this app's navigation; never grows dynamically

struct StackEntry {
    ScreenFactory factory;
    void* arg;
    const char* titleOverride;
};

StackEntry g_stack[MAX_DEPTH];
int g_depth = 0;

lv_obj_t* g_root = nullptr;
lv_obj_t* g_header = nullptr;
lv_obj_t* g_headerBackBtn = nullptr;
lv_obj_t* g_headerTitle = nullptr;
lv_obj_t* g_content = nullptr;

Screen* g_currentScreen = nullptr;  // owned; the only live Screen instance at any time

constexpr lv_coord_t STATUS_BAR_H = 46;
constexpr lv_coord_t HEADER_H = 56;

void onBackClicked(lv_event_t* e) {
    LV_UNUSED(e);
    pop();
}

void buildHeader() {
    g_header = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_header);
    lv_obj_add_style(g_header, theme::screenBg(), 0);
    lv_obj_set_size(g_header, LV_PCT(100), HEADER_H);
    lv_obj_set_pos(g_header, 0, STATUS_BAR_H);
    lv_obj_clear_flag(g_header, LV_OBJ_FLAG_SCROLLABLE);

    // 1px hairline at the header's bottom edge — a real divider strip, not
    // a full-fill style misapplied to the whole header.
    lv_obj_t* hairline = lv_obj_create(g_header);
    lv_obj_remove_style_all(hairline);
    lv_obj_add_style(hairline, theme::divider(), 0);
    lv_obj_set_size(hairline, LV_PCT(100), 1);
    lv_obj_align(hairline, LV_ALIGN_BOTTOM_MID, 0, 0);

    g_headerBackBtn = lv_btn_create(g_header);
    lv_obj_add_style(g_headerBackBtn, theme::btnSecondary(), 0);
    lv_obj_add_style(g_headerBackBtn, theme::btnSecondaryPressed(), LV_STATE_PRESSED);
    lv_obj_set_size(g_headerBackBtn, 74, 44);
    lv_obj_align(g_headerBackBtn, LV_ALIGN_LEFT_MID, 12, 0);
    // Plain ASCII, not LV_SYMBOL_LEFT — the embedded fonts only cover
    // ASCII 0x20-0x7E (see fonts/fonts.h); LVGL's symbol glyphs live in a
    // different codepoint range this project deliberately didn't convert.
    lv_obj_t* backLabel = lv_label_create(g_headerBackBtn);
    lv_obj_set_style_text_font(backLabel, &ui_font_plex_600_23, 0);
    lv_label_set_text(backLabel, "< Back");
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(g_headerBackBtn, onBackClicked, LV_EVENT_CLICKED, nullptr);

    g_headerTitle = lv_label_create(g_header);
    lv_obj_add_style(g_headerTitle, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(g_headerTitle, &ui_font_archivo_700_23, 0);
    lv_obj_align(g_headerTitle, LV_ALIGN_CENTER, 0, 0);
}

void refreshHeader() {
    const StackEntry& top = g_stack[g_depth - 1];
    const char* title = top.titleOverride != nullptr ? top.titleOverride : g_currentScreen->title();
    lv_label_set_text(g_headerTitle, title);
    if (g_depth > 1) {
        lv_obj_clear_flag(g_headerBackBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_headerBackBtn, LV_OBJ_FLAG_HIDDEN);  // root screen: nothing to go back to
    }
}

void destroyCurrent() {
    if (g_currentScreen != nullptr) {
        g_currentScreen->teardown();
        delete g_currentScreen;
        g_currentScreen = nullptr;
    }
    if (g_content != nullptr) {
        lv_obj_del(g_content);  // recursively frees every widget the screen created — the whole leak-prevention mechanism
        g_content = nullptr;
    }
}

void buildCurrent() {
    g_content = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_content);
    lv_obj_add_style(g_content, theme::screenBg(), 0);
    lv_obj_set_size(g_content, LV_PCT(100), lv_disp_get_ver_res(nullptr) - STATUS_BAR_H - HEADER_H);
    lv_obj_set_pos(g_content, 0, STATUS_BAR_H + HEADER_H);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);

    const StackEntry& top = g_stack[g_depth - 1];
    g_currentScreen = top.factory(top.arg);
    g_currentScreen->build(g_content);
    refreshHeader();
    g_currentScreen->onShow();
}

}  // namespace

void init() {
    g_root = lv_scr_act();
    lv_obj_remove_style_all(g_root);
    lv_obj_add_style(g_root, theme::screenBg(), 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);

    components::buildStatusBar(g_root, STATUS_BAR_H);
    buildHeader();
}

void push(ScreenFactory factory, void* arg, const char* title) {
    if (g_depth >= MAX_DEPTH) return;  // never crash on a runaway push chain — just stop navigating deeper
    destroyCurrent();
    g_stack[g_depth] = StackEntry{factory, arg, title};
    g_depth++;
    buildCurrent();
}

void pop() {
    if (g_depth <= 1) return;  // can't pop the root
    destroyCurrent();
    g_depth--;
    buildCurrent();
}

void popToRoot() {
    if (g_depth <= 1) return;
    destroyCurrent();
    g_depth = 1;
    buildCurrent();
}

void refresh() {
    destroyCurrent();
    buildCurrent();
}

int depth() { return g_depth; }

}  // namespace ui::screen_manager
