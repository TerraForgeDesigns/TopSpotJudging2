# DESIGN.md — Visual Design System

This is the design system for both the home base web UI and the handheld touchscreen
UI. Read [CONTEXT.md](CONTEXT.md) first if you haven't. This document is the source of
truth for color, type, spacing, and components on both platforms — implementations
should converge on it rather than drift into platform-specific choices.

## Brand

**Top Spot Judging.** The feel is premium automotive — a well-lit show car at dusk, not
a spreadsheet. Dark surfaces, a warm metallic gold as the signature accent (it reads as
trophy/first-place without being literal), crisp white type, and restrained use of
color so that status colors actually mean something when they appear.

**Hard rule: gold is for brand and primary action only.** Never use gold to indicate a
status. Status uses green/blue/red exclusively. This means a gold element always means
"this is the thing to press," and a colored dot always means "this is a state." Do not
let this erode over time — it's what keeps the UI legible at a glance.

## Color tokens

Define these as CSS custom properties in the web app, and as named color constants in
firmware (e.g. `COLOR_INK_900`, `COLOR_GOLD_500`).

| Token | Hex / value | Usage |
|---|---|---|
| `--ink-900` | `#0E1116` | App background, deepest |
| `--ink-800` | `#171B22` | Card / panel surface |
| `--ink-700` | `#1F242D` | Raised surface, inputs |
| `--ink-600` | `#2A313C` | Borders, dividers |
| `--ink-400` | `#667085` | Disabled text, subtle icons only — see accessibility note below |
| `--text-secondary` | `#98A2B3` | Secondary text |
| `--text-primary` | `#F2F4F7` | Primary text |
| `--gold-500` | `#F5A524` | **Primary brand accent** — primary buttons, active states, focus rings, logo mark |
| `--gold-400` | `#FFB84D` | Hover/active state of gold elements |
| `--gold-glow` | `rgba(245, 165, 36, 0.15)` | Subtle glow/halo behind emphasized elements |
| `--green-500` | `#12B76A` | Status: judged, synced, success |
| `--blue-500` | `#2E90FA` | Status: pending sync, informational |
| `--red-500` | `#F04438` | Status: conflict, error, unjudged-critical |

## Typography — web (home base)

Self-host font files in the repo. Do **not** link Google Fonts or any other font
service at runtime. Download the `.woff2` files into
`/homebase/app/static/fonts/` and declare them with `@font-face` plus a system-font
fallback stack, e.g.:

```css
@font-face {
  font-family: 'Archivo';
  src: url('/static/fonts/archivo-700.woff2') format('woff2');
  font-weight: 700;
  font-display: swap;
}
/* fallback stack for all three roles: */
/* -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif */
```

Three roles, two families:

- **Archivo** (weights 600/700/800) — display face. Headings, car numbers, scores,
  award titles. An industrial grotesque with the density of signage and racing
  numbers — that's the point.
- **IBM Plex Sans** (weights 400/500/600) — UI and body text. Technical, precise,
  reads well at small sizes on a laptop screen outdoors.
- **IBM Plex Mono** (weights 400/500) — small uppercase labels, registration numbers,
  timestamps, anything tabular or technical. Use letter-spacing ~`.14em` on uppercase
  mono labels.

**Type scale:** 11 / 13 / 16 / 20 / 24 / 32 / 52 / 64 px. Body text is 16px minimum.
Use `font-variant-numeric: tabular-nums` anywhere digits line up in a column — scores
and car numbers appear in tables constantly.

## Typography — handheld

The display library's built-in fonts are too small and too ugly for outdoor use at
arm's length. Convert Archivo (display/numeric) and IBM Plex Sans (body) to the display
library's font format at **3 sizes minimum**:

| Role | Approx size |
|---|---|
| Body | 16px equivalent |
| Heading | 23px equivalent |
| Big numeric readout (score, car number) | 44px equivalent |

Nothing on the handheld should render smaller than ~13px equivalent — it's read at
arm's length, outdoors, possibly by someone over 50 without their glasses.

**The largest type on any handheld screen must be the data** (car number, current
score) — never a label, and never the branding.

## Layout & components — web

- 8px spacing grid.
- Corner radius: cards 12px, buttons 8px, inputs 8px.
- **Cards:** `--ink-800` surface, 1px `--ink-600` border, subtle shadow, 20–24px
  internal padding.
- **Primary button:** gold fill, dark text (`#0E1116` — gold is too light for white
  text to pass contrast), 8px radius, clear hover/active/focus states. Focus ring must
  be visible for keyboard use.
- **Secondary button:** transparent fill, `--ink-600` border, `--text-primary` text.
- **Destructive:** red text/border; filled red only on confirm dialogs.
- **Inputs:** `--ink-700` fill, `--ink-600` border, gold border on focus. Never
  default browser styling.
- **Status pills:** small rounded chips with a colored dot + label — Judged / Pending
  / Conflict / Unjudged.
- **Data tables:** no heavy grid lines. Row separation via 1px `--ink-600` bottom
  border and hover highlight. Right-align numbers. Tabular figures for scores.
- **Motion:** 150–200ms ease transitions on hover/state change. Nothing bouncy,
  nothing slow.

## Layout & components — handheld touchscreen

- Touch targets minimum 44×44px equivalent. Judges are wearing sunglasses, standing
  up, possibly holding a paper form in the other hand — assume one-handed use with a
  thumb.
- One primary action per screen: big, gold, at the bottom where a thumb rests.
- High contrast: light text on `--ink-900`. Avoid mid-gray text on the handheld
  entirely — `--text-secondary` is the lightest secondary color allowed.
- **Daylight Mode:** an alternate high-contrast theme (near-black text on a
  white/very light background) for direct sunlight, where a dark theme washes out on a
  small TFT. This is a toggle, stored as a preference on the SD card. This is a real
  usability requirement outdoors, not a nice-to-have.
- Numeric keypad for car number entry needs very large keys — this is the most-used
  screen in the app.
- Persistent status bar across the top: battery %, sync state icon, show progress
  count (e.g. "142 / 310 judged").

## Awards presentation mode (web, full screen)

Shown full screen on a projector or a big TV, cinematic and dark:

- `--ink-900` background, large car photo as the hero element, gold accents.
- Car number and award title in very large display type (48–64px+), legible from the
  back of a room.
- Score breakdown presented as a clean list, not a data table — this is being shown to
  an audience, not analyzed.
- Judge sheet photo available as a secondary/toggleable view, not competing with the
  car photo.
- Smooth 200–300ms crossfade between cars. No slide-in animations, no page flash.

## Accessibility

All text/background pairs must meet WCAG AA (4.5:1 body text, 3:1 large text). These
ratios are pre-measured against `--ink-900` (`#0E1116`) — record and defend them here
so they don't erode with future tweaks:

| Token | Hex | Contrast vs `--ink-900` | Verdict |
|---|---|---|---|
| `--text-primary` | `#F2F4F7` | 17.2:1 | Passes everything |
| `--text-secondary` | `#98A2B3` | 7.3:1 | Passes AA body |
| `--gold-500` | `#F5A524` | 9.3:1 | Passes AA body |
| `--green-500` | `#12B76A` | 7.2:1 | Passes AA body |
| `--blue-500` | `#2E90FA` | 5.8:1 | Passes AA body |
| `--red-500` | `#F04438` | 5.0:1 | Passes AA body |
| `--ink-400` | `#667085` | 3.8:1 | **Fails AA body** — restrict to disabled states, large text, and decorative icons only. Never body copy. |

Dark text (`#0E1116`) on a gold fill: **9.3:1**. White text on gold is only **2.0:1** —
that combination is forbidden. Always use dark text on gold fills.
