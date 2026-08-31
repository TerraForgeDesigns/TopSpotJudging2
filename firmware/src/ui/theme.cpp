// See theme.h. Daylight Mode's exact hex values are new to this task —
// DESIGN.md only describes it qualitatively ("near-black text on a
// white/very light background"); nothing in DESIGN.md or DECISIONS.md
// specified real numbers before now. See DECISIONS.md for the values
// chosen and the reasoning (gold and the three status colors are
// deliberately IDENTICAL in both themes — brand and semantic-status colors
// don't shift with ambient light, only surfaces and text do).
#include "theme.h"

#include <cstring>

#include "storage/sd_card.h"

namespace ui::theme {

namespace {

constexpr const char* PREF_PATH = "/prefs/theme.txt";

Palette g_dark = {
    .ink900 = lv_color_hex(0x0E1116),
    .ink800 = lv_color_hex(0x171B22),
    .ink700 = lv_color_hex(0x1F242D),
    .ink600 = lv_color_hex(0x2A313C),
    .ink400 = lv_color_hex(0x667085),

    .textPrimary = lv_color_hex(0xF2F4F7),
    .textSecondary = lv_color_hex(0x98A2B3),
    .textOnGold = lv_color_hex(0x0E1116),

    .gold500 = lv_color_hex(0xF5A524),
    .gold400 = lv_color_hex(0xFFB84D),

    .green500 = lv_color_hex(0x12B76A),
    .blue500 = lv_color_hex(0x2E90FA),
    .red500 = lv_color_hex(0xF04438),
};

Palette g_daylight = {
    // A light-gray app background, not stark white — reduces glare
    // outdoors while still reading as "high contrast."
    .ink900 = lv_color_hex(0xF5F6F8),
    .ink800 = lv_color_hex(0xFFFFFF),  // cards: pure white, pops against the app bg
    .ink700 = lv_color_hex(0xFBFBFC),  // inputs/raised surfaces
    .ink600 = lv_color_hex(0xD0D5DD),  // borders — visible against white
    .ink400 = lv_color_hex(0x98A2B3),  // disabled — same mid-gray as dark mode's secondary text

    .textPrimary = lv_color_hex(0x0E1116),    // dark mode's ink900 becomes light mode's darkest text
    .textSecondary = lv_color_hex(0x475467),
    .textOnGold = lv_color_hex(0x0E1116),     // unchanged — always dark on gold, both themes

    // Brand and status colors are IDENTICAL to dark mode — they're
    // semantic/brand colors, not surface colors, so they don't shift with
    // ambient light. Only the surfaces and text around them do.
    .gold500 = lv_color_hex(0xF5A524),
    .gold400 = lv_color_hex(0xFFB84D),
    .green500 = lv_color_hex(0x12B76A),
    .blue500 = lv_color_hex(0x2E90FA),
    .red500 = lv_color_hex(0xF04438),
};

Mode g_mode = Mode::Dark;

lv_style_t g_screenBg, g_card, g_raisedSurface;
lv_style_t g_btnPrimary, g_btnPrimaryPressed, g_btnSecondary, g_btnSecondaryPressed, g_btnDestructive;
lv_style_t g_textPrimary, g_textSecondary, g_divider;
lv_style_t g_statusGood, g_statusPending, g_statusBad;
lv_style_t g_bannerInfo, g_bannerCritical;

void applyPaletteToStyles() {
    const Palette& p = g_mode == Mode::Dark ? g_dark : g_daylight;

    lv_style_set_bg_color(&g_screenBg, p.ink900);
    lv_style_set_bg_opa(&g_screenBg, LV_OPA_COVER);

    lv_style_set_bg_color(&g_card, p.ink800);
    lv_style_set_border_color(&g_card, p.ink600);
    lv_style_set_border_width(&g_card, 1);
    lv_style_set_radius(&g_card, 12);
    lv_style_set_bg_opa(&g_card, LV_OPA_COVER);

    lv_style_set_bg_color(&g_raisedSurface, p.ink700);
    lv_style_set_border_color(&g_raisedSurface, p.ink600);
    lv_style_set_border_width(&g_raisedSurface, 1);
    lv_style_set_radius(&g_raisedSurface, 8);
    lv_style_set_bg_opa(&g_raisedSurface, LV_OPA_COVER);

    // Primary button: gold fill, dark text always — DESIGN.md's contrast
    // rule (white-on-gold is 2.0:1, forbidden) is structurally
    // unreachable here, since textOnGold is hardwired dark in both themes.
    lv_style_set_bg_color(&g_btnPrimary, p.gold500);
    lv_style_set_bg_opa(&g_btnPrimary, LV_OPA_COVER);
    lv_style_set_text_color(&g_btnPrimary, p.textOnGold);
    lv_style_set_radius(&g_btnPrimary, 8);
    lv_style_set_border_width(&g_btnPrimary, 0);

    lv_style_set_bg_color(&g_btnPrimaryPressed, p.gold400);
    lv_style_set_bg_opa(&g_btnPrimaryPressed, LV_OPA_COVER);
    lv_style_set_text_color(&g_btnPrimaryPressed, p.textOnGold);
    lv_style_set_radius(&g_btnPrimaryPressed, 8);

    lv_style_set_bg_opa(&g_btnSecondary, LV_OPA_TRANSP);
    lv_style_set_border_color(&g_btnSecondary, p.ink600);
    lv_style_set_border_width(&g_btnSecondary, 1);
    lv_style_set_text_color(&g_btnSecondary, p.textPrimary);
    lv_style_set_radius(&g_btnSecondary, 8);

    lv_style_set_bg_opa(&g_btnSecondaryPressed, LV_OPA_COVER);
    lv_style_set_bg_color(&g_btnSecondaryPressed, p.ink700);
    lv_style_set_border_color(&g_btnSecondaryPressed, p.gold500);
    lv_style_set_border_width(&g_btnSecondaryPressed, 1);
    lv_style_set_text_color(&g_btnSecondaryPressed, p.textPrimary);
    lv_style_set_radius(&g_btnSecondaryPressed, 8);

    // White-on-red text pairing here isn't a DESIGN.md-recorded/measured
    // contrast ratio (only the dark-on-gold pairing is) — a reasonable,
    // conventional choice for a saturated red fill, not a verified one.
    lv_style_set_bg_color(&g_btnDestructive, p.red500);
    lv_style_set_bg_opa(&g_btnDestructive, LV_OPA_COVER);
    lv_style_set_text_color(&g_btnDestructive, p.textPrimary);
    lv_style_set_radius(&g_btnDestructive, 8);
    lv_style_set_border_width(&g_btnDestructive, 0);

    lv_style_set_text_color(&g_textPrimary, p.textPrimary);
    lv_style_set_text_color(&g_textSecondary, p.textSecondary);

    lv_style_set_bg_color(&g_divider, p.ink600);
    lv_style_set_bg_opa(&g_divider, LV_OPA_COVER);

    lv_style_set_bg_color(&g_statusGood, p.green500);
    lv_style_set_bg_opa(&g_statusGood, LV_OPA_COVER);
    lv_style_set_bg_color(&g_statusPending, p.blue500);
    lv_style_set_bg_opa(&g_statusPending, LV_OPA_COVER);
    lv_style_set_bg_color(&g_statusBad, p.red500);
    lv_style_set_bg_opa(&g_statusBad, LV_OPA_COVER);

    lv_style_set_bg_color(&g_bannerInfo, p.ink800);
    lv_style_set_bg_opa(&g_bannerInfo, LV_OPA_COVER);
    lv_style_set_border_color(&g_bannerInfo, p.blue500);
    lv_style_set_border_width(&g_bannerInfo, 3);
    lv_style_set_border_side(&g_bannerInfo, LV_BORDER_SIDE_LEFT);
    lv_style_set_radius(&g_bannerInfo, 8);

    lv_style_set_bg_color(&g_bannerCritical, p.ink800);
    lv_style_set_bg_opa(&g_bannerCritical, LV_OPA_COVER);
    lv_style_set_border_color(&g_bannerCritical, p.red500);
    lv_style_set_border_width(&g_bannerCritical, 3);
    lv_style_set_border_side(&g_bannerCritical, LV_BORDER_SIDE_LEFT);
    lv_style_set_radius(&g_bannerCritical, 8);
}

void persist(Mode mode) {
    const char* text = mode == Mode::Dark ? "dark" : "daylight";
    storage::writeFile(PREF_PATH, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

Mode loadPersisted() {
    char buf[16] = {0};
    int n = storage::readFile(PREF_PATH, reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
    if (n <= 0) return Mode::Dark;  // no preference yet, or SD unavailable — never fail startup over this
    return (strncmp(buf, "daylight", 8) == 0) ? Mode::Daylight : Mode::Dark;
}

}  // namespace

void init() {
    lv_style_init(&g_screenBg);
    lv_style_init(&g_card);
    lv_style_init(&g_raisedSurface);
    lv_style_init(&g_btnPrimary);
    lv_style_init(&g_btnPrimaryPressed);
    lv_style_init(&g_btnSecondary);
    lv_style_init(&g_btnSecondaryPressed);
    lv_style_init(&g_btnDestructive);
    lv_style_init(&g_textPrimary);
    lv_style_init(&g_textSecondary);
    lv_style_init(&g_divider);
    lv_style_init(&g_statusGood);
    lv_style_init(&g_statusPending);
    lv_style_init(&g_statusBad);
    lv_style_init(&g_bannerInfo);
    lv_style_init(&g_bannerCritical);

    g_mode = loadPersisted();
    applyPaletteToStyles();
}

Mode currentMode() { return g_mode; }

const Palette& colors() { return g_mode == Mode::Dark ? g_dark : g_daylight; }

void setMode(Mode mode) {
    if (mode == g_mode) return;
    g_mode = mode;
    applyPaletteToStyles();
    // Every live object using one of the styles above redraws with the
    // new colors immediately — no screen needs to rebuild or re-fetch
    // anything. This is the whole mechanism behind "runtime, no
    // per-screen changes."
    lv_obj_report_style_change(nullptr);
    persist(mode);
}

lv_style_t* screenBg() { return &g_screenBg; }
lv_style_t* card() { return &g_card; }
lv_style_t* raisedSurface() { return &g_raisedSurface; }
lv_style_t* btnPrimary() { return &g_btnPrimary; }
lv_style_t* btnPrimaryPressed() { return &g_btnPrimaryPressed; }
lv_style_t* btnSecondary() { return &g_btnSecondary; }
lv_style_t* btnSecondaryPressed() { return &g_btnSecondaryPressed; }
lv_style_t* btnDestructive() { return &g_btnDestructive; }
lv_style_t* textPrimary() { return &g_textPrimary; }
lv_style_t* textSecondary() { return &g_textSecondary; }
lv_style_t* divider() { return &g_divider; }
lv_style_t* statusGood() { return &g_statusGood; }
lv_style_t* statusPending() { return &g_statusPending; }
lv_style_t* statusBad() { return &g_statusBad; }
lv_style_t* bannerInfo() { return &g_bannerInfo; }
lv_style_t* bannerCritical() { return &g_bannerCritical; }

}  // namespace ui::theme
