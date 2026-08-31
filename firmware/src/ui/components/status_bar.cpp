#include "status_bar.h"

#include <cstdio>
#include <cstring>

#include "../fonts/fonts.h"
#include "../theme.h"

namespace ui::components {

namespace {

lv_obj_t* g_bar = nullptr;
lv_obj_t* g_deviceLabel = nullptr;
lv_obj_t* g_judgeLabel = nullptr;
lv_obj_t* g_connDot = nullptr;
lv_obj_t* g_connLabel = nullptr;
lv_obj_t* g_batteryDot = nullptr;
lv_obj_t* g_batteryLabel = nullptr;

// A small colored circle + a short uppercase label, e.g. the connection
// or battery indicator — DESIGN.md's status-pill shape, right-sized for
// the status bar rather than a full pill chip.
lv_obj_t* buildDotLabel(lv_obj_t* parent, lv_obj_t** outDot, lv_obj_t** outLabel) {
    lv_obj_t* group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group, 6, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dot = lv_obj_create(group);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(dot, theme::statusGood(), 0);  // default; caller sets the real state

    lv_obj_t* label = lv_label_create(group);
    lv_obj_add_style(label, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(label, &ui_font_plex_500_19, 0);

    *outDot = dot;
    *outLabel = label;
    return group;
}

}  // namespace

void buildStatusBar(lv_obj_t* parent, lv_coord_t height) {
    g_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(g_bar);
    // Reuses card()'s bg_color (ink800) and border_color (ink600) as the
    // live, theme-reactive source for both — only geometry is overridden
    // locally (square corners, bottom edge only), never a color, so this
    // still repaints correctly on theme::setMode().
    lv_obj_add_style(g_bar, theme::card(), 0);
    lv_obj_set_style_radius(g_bar, 0, 0);
    lv_obj_set_style_border_side(g_bar, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_set_size(g_bar, LV_PCT(100), height);
    lv_obj_set_pos(g_bar, 0, 0);
    lv_obj_set_style_pad_hor(g_bar, 16, 0);
    lv_obj_clear_flag(g_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(g_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left group: device label + judge label.
    lv_obj_t* left = lv_obj_create(g_bar);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    g_deviceLabel = lv_label_create(left);
    lv_obj_add_style(g_deviceLabel, theme::textPrimary(), 0);
    lv_obj_set_style_text_font(g_deviceLabel, &ui_font_plex_600_23, 0);
    lv_label_set_text(g_deviceLabel, "Unnamed");

    g_judgeLabel = lv_label_create(left);
    lv_obj_add_style(g_judgeLabel, theme::textSecondary(), 0);
    lv_obj_set_style_text_font(g_judgeLabel, &ui_font_plex_400_16, 0);
    lv_label_set_text(g_judgeLabel, "");

    // Right group: connection state + battery.
    lv_obj_t* right = lv_obj_create(g_bar);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 18, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    buildDotLabel(right, &g_connDot, &g_connLabel);
    buildDotLabel(right, &g_batteryDot, &g_batteryLabel);

    setConnState(ConnState::NotConnected);  // honest default until the caller reports real state
    setBatteryPct(-1);
}

void setDeviceLabel(const char* text) {
    if (g_deviceLabel == nullptr) return;
    lv_label_set_text(g_deviceLabel, (text != nullptr && text[0] != '\0') ? text : "Unnamed");
}

void setJudgeLabel(const char* text) {
    if (g_judgeLabel == nullptr) return;
    lv_label_set_text(g_judgeLabel, text != nullptr ? text : "");
}

void setConnState(ConnState state) {
    if (g_connDot == nullptr) return;
    lv_obj_remove_style(g_connDot, theme::statusGood(), 0);
    lv_obj_remove_style(g_connDot, theme::statusPending(), 0);
    lv_obj_remove_style(g_connDot, theme::statusBad(), 0);
    switch (state) {
        case ConnState::UpToDate:
            lv_obj_add_style(g_connDot, theme::statusGood(), 0);
            lv_label_set_text(g_connLabel, "UP TO DATE");
            break;
        case ConnState::Updating:
            lv_obj_add_style(g_connDot, theme::statusPending(), 0);
            lv_label_set_text(g_connLabel, "UPDATING");
            break;
        case ConnState::NotConnected:
            lv_obj_add_style(g_connDot, theme::statusBad(), 0);
            lv_label_set_text(g_connLabel, "NOT CONNECTED");
            break;
    }
}

void setBatteryPct(int pct) {
    if (g_batteryDot == nullptr) return;
    char buf[16];
    lv_obj_remove_style(g_batteryDot, theme::statusGood(), 0);
    lv_obj_remove_style(g_batteryDot, theme::statusPending(), 0);
    lv_obj_remove_style(g_batteryDot, theme::statusBad(), 0);
    if (pct < 0) {
        lv_obj_add_style(g_batteryDot, theme::statusPending(), 0);
        snprintf(buf, sizeof(buf), "--");
    } else if (pct <= 15) {
        lv_obj_add_style(g_batteryDot, theme::statusBad(), 0);
        snprintf(buf, sizeof(buf), "%d%%", pct);
    } else {
        lv_obj_add_style(g_batteryDot, theme::statusGood(), 0);
        snprintf(buf, sizeof(buf), "%d%%", pct);
    }
    lv_label_set_text(g_batteryLabel, buf);
}

}  // namespace ui::components
