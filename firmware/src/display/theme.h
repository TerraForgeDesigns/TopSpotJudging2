// Color theme — RGB565 constants converted directly from DESIGN.md's
// token table (the same source of truth the home base web UI uses), so the
// handheld and home base stay visually consistent. Values below were
// computed programmatically from DESIGN.md's hex values, not hand-converted
// — see the standard RGB888->RGB565 formula:
//   ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)
//
// DESIGN.md's hard rule still applies here: gold is brand/primary-action
// ONLY, never a status indicator. Status uses green/blue/red exclusively.
//
// Daylight Mode (DESIGN.md handheld section) is a separate palette, not
// included here yet — added when the UI screens that need it are built.
#pragma once

#include <cstdint>

namespace theme {

// Ink scale (surfaces)
constexpr uint16_t INK_900 = 0x0882;  // #0E1116 app background, deepest
constexpr uint16_t INK_800 = 0x10C4;  // #171B22 card/panel surface
constexpr uint16_t INK_700 = 0x1925;  // #1F242D raised surface, inputs
constexpr uint16_t INK_600 = 0x2987;  // #2A313C borders, dividers
constexpr uint16_t INK_400 = 0x6390;  // #667085 disabled/subtle only — fails AA for body text

// Text
constexpr uint16_t TEXT_PRIMARY = 0xF7BE;    // #F2F4F7
constexpr uint16_t TEXT_SECONDARY = 0x9D16;  // #98A2B3
constexpr uint16_t TEXT_ON_GOLD = INK_900;   // dark text on gold fills — see DESIGN.md contrast rule

// Brand (primary action only — never status)
constexpr uint16_t GOLD_500 = 0xF524;  // #F5A524
constexpr uint16_t GOLD_400 = 0xFDC9;  // #FFB84D hover/active

// Status (never used for brand/primary action)
constexpr uint16_t GREEN_500 = 0x15AD;  // #12B76A judged/synced/success
constexpr uint16_t BLUE_500 = 0x2C9F;   // #2E90FA pending sync/informational
constexpr uint16_t RED_500 = 0xF227;    // #F04438 conflict/error

// Type scale (DESIGN.md handheld sizes) — approximate point sizes for the
// converted display-library fonts; nothing on the handheld renders smaller
// than SIZE_BODY, per DESIGN.md ("read at arm's length, outdoors").
constexpr uint8_t SIZE_BODY = 16;
constexpr uint8_t SIZE_HEADING = 23;
constexpr uint8_t SIZE_READOUT = 44;  // big numeric readouts: car number, score

}  // namespace theme
