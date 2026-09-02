// lcars_theme — shared LCARS-inspired color palette, panel/elbow/pill
// drawing helpers, and font choices. See CLAUDE.md "Design language". This
// is the only place in the project that defines colors/shapes/fonts —
// table_view and (later) radar_view call into this rather than picking
// their own.
#pragma once

#include <cstdint>

#include "flight_phase.h"  // Phase — for phaseIcon()
#include "touch_input.h"   // Rect — shared tappable-region type

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Converts 8-bit RGB channels to a 16-bit RGB565 color — the format used
// by both TFT_eSPI and LovyanGFX, and thus by the palette constants below.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// The project's palette — the exact RGB565 values from CLAUDE.md "Design
// language" ("exact values, not vague color names"). Every screen spec
// (Screen 1/2/3) refers to these names, so they are the canonical set;
// define colors once here rather than scattering raw hex literals.
constexpr uint16_t LCARS_BLACK = 0x0000;    // background
constexpr uint16_t LCARS_ORANGE = 0xFC00;   // header block, column headers, divider, home marker
constexpr uint16_t LCARS_MAGENTA = 0xF81F;  // elbow-frame borders (featured / identity / summary panels)
constexpr uint16_t LCARS_CYAN = 0x07FF;     // sub-headers, stardate/time text, radar blips
constexpr uint16_t LCARS_YELLOW = 0xFFE0;   // radar home crosshair, "imminent" countdown

// Shared header-bar height (see CLAUDE.md "Screen chrome"): status_bar
// draws y:0..LCARS_HEADER_HEIGHT, every screen's content starts at
// y = LCARS_HEADER_HEIGHT so all three stay visually aligned.
constexpr int16_t LCARS_HEADER_HEIGHT = 25;

// Height of the persistent bottom nav bar (CLAUDE.md "Screen navigation").
// Every screen's content area is bounded below by this: it runs from
// y = LCARS_HEADER_HEIGHT (28) to y = 240 - LCARS_BOTTOM_NAV_HEIGHT (225),
// NOT all the way to 240 — the bar draws in y:225..240. (screen_nav gives
// its touch hit-zone a bit more vertical slack than these 15px.)
constexpr int16_t LCARS_BOTTOM_NAV_HEIGHT = 15;

// Bottom-nav inactive-segment fill - a dim slate so unselected segments
// read as recessed against LCARS_BLACK while the active one (LCARS_ORANGE)
// pops. Not part of CLAUDE.md's five-colour palette (that's the canonical
// set above); this is a supporting shade, same role as the legacy tints.
constexpr uint16_t LCARS_NAV_INACTIVE = 0x2124;

// --- Legacy palette (predates the canonical values above). Still
// referenced by featured_panel / table_view / radar_view / main; those
// modules should migrate to the canonical names, after which these can be
// removed. Kept for now so this change stays scoped to lcars_theme.
constexpr uint16_t LCARS_AMBER = rgb565(0xFF, 0x99, 0x00);        // ~ LCARS_ORANGE
constexpr uint16_t LCARS_BLUE_VIOLET = rgb565(0x99, 0x66, 0xFF);  // pill buttons
constexpr uint16_t LCARS_ROSE = rgb565(0xFF, 0x66, 0x66);         // ~ LCARS_MAGENTA
constexpr uint16_t LCARS_PALE_BLUE = rgb565(0x99, 0xCC, 0xFF);    // ~ LCARS_CYAN

// A point on the elbow's connecting arc, relative to the arc's center.
struct ElbowArcPoint {
  int16_t x = 0;
  int16_t y = 0;
};

// Point at `angle_deg` (standard math convention: 0 = +x axis/east, 90 =
// +y axis/south — i.e. clockwise on screen, since y grows downward) on a
// circle of the given radius, rounded to the nearest pixel. Used to
// size/position drawElbow()'s straight bar segments so they meet its
// curve without a gap or overlap. Pure — no TFT dependency — tested under
// `pio test -e native`.
ElbowArcPoint elbowArcPoint(int16_t radius, float angle_deg);

// On-screen bounds of the view-toggle button placeholder (top-right
// corner) for a `screenWidth` x `screenHeight` display. Pure — shared by
// drawViewToggleButton() below and touch hit-testing (touch_input's
// hitTest()) so the drawn button and its tap target never disagree. Will
// eventually toggle table_view/radar_view; for now it only logs a Serial
// message (see main.cpp) — see CLAUDE.md "Visualization concept".
Rect viewToggleButtonBounds(int16_t screenWidth, int16_t screenHeight);

// One-character glyph for a flight phase — the single place this project
// decides how a Phase looks on screen, so featured_panel and table_view
// (both of which display a phase icon per CLAUDE.md "Screen 1") can't
// silently disagree. Kept ASCII (bitmap GLCD/BMP fonts generally only
// cover 0x20-0x7E — see table_view's orDash()) rather than a fancier
// glyph. Pure — no TFT dependency — tested under `pio test -e native`.
char phaseIcon(Phase phase);

// ---- Hardware adapter: actual drawing, via LovyanGFX. Not covered by
// Unity (see CLAUDE.md "Testing") — this module is almost entirely draw
// calls, which is expected for a theme/chrome module. Requires the LGFX
// device type, so this whole section (unlike other modules' adapters) is
// guarded in the header too, keeping the palette/elbowArcPoint() above
// free of any ESP32/LovyanGFX dependency for `pio test -e native`.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Fonts used across the project, chosen once here rather than per-view —
// table_view/radar_view reference these instead of picking their own.
extern const lgfx::IFont *const LCARS_FONT_HEADING;  // panel/header titles
extern const lgfx::IFont *const LCARS_FONT_BODY;     // table rows, labels
extern const lgfx::IFont *const LCARS_FONT_NUMERIC;  // altitude/speed readouts (7-seg style)

// Rounded-rectangle panel — the basic content-block shape used throughout
// both views.
void drawPanel(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// The LCARS "elbow": a vertical bar of `barWidth` running down from
// (x, y) for `height` pixels, curving at the top-left into a horizontal
// header bar of the same thickness running right for `headerWidth`
// pixels. The persistent frame element shared by both views (see
// CLAUDE.md "Design language" — chrome stays constant across screens).
void drawElbow(LGFX &gfx, int16_t x, int16_t y, int16_t height, int16_t headerWidth,
               int16_t barWidth, uint16_t color);

// Pill-shaped button/label: a fully rounded rectangle (corner radius =
// h/2) with text centered inside. Used for the view-toggle button and
// section headers/labels.
void drawPillButton(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fillColor,
                     uint16_t textColor, const char *text);

// Draws the view-toggle button placeholder at viewToggleButtonBounds().
// Purely visual — tapping it is handled by main.cpp (touch_input::hitTest()
// against viewToggleButtonBounds()), not by this module.
void drawViewToggleButton(LGFX &gfx, int16_t screenWidth, int16_t screenHeight);

// The LCARS swept-corner "elbow frame": a hollow rectangular border
// `thickness` px thick around (x, y, w, h), with the top-left corner
// swept into a quarter-circle of radius `cornerRadius` instead of a plain
// right angle. The other three corners stay square. This is the frame
// around Screen 1's featured panel, Screen 2's identity panel and Screen
// 3's summary panel (all `LCARS_MAGENTA`) — see CLAUDE.md "Design
// language" / "Visualization concept".
//
// Drawn as four `fillRect` edges plus one `fillArc` quarter-annulus for
// the corner (inner radius `cornerRadius - thickness`, outer
// `cornerRadius`, swept 180deg->270deg — the top-left quadrant — per
// LovyanGFX 1.2.28's fillArc(x, y, r0, r1, angle0, angle1, color) —
// verified in src/lgfx/v1/LGFXBase.{hpp,cpp}: it forwards to
// fillEllipseArc(), which swaps r0/r1 so inner/outer order doesn't
// matter; angles are degrees, 0 = east, increasing clockwise with screen
// y down). The seam
// geometry is exposed pure via elbowArcPoint() for tests.
void drawElbowFrame(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t cornerRadius, int16_t thickness, uint16_t color);

// Vertical accent/divider bar — a filled `thickness`-px-wide column of
// height `h` at (x, y). Screen 3 uses one in `LCARS_ORANGE` (x:192,
// 8px wide, full content height) as the split between the radar and the
// summary panel; also the generic "elbow sidebar" motif.
void drawVerticalDivider(LGFX &gfx, int16_t x, int16_t y, int16_t h, int16_t thickness,
                         uint16_t color);

// One radar range ring: a `drawCircle` outline at `radiusPx` around
// (centerX, centerY), plus its distance in whole km labelled in small
// text at the ring's 12-o'clock point (per CLAUDE.md "Screen 3 —
// Radar"). Call once per ring from computeRingDistances()' output.
void drawRadarRing(LGFX &gfx, int16_t centerX, int16_t centerY, int16_t radiusPx,
                   int distanceKm, uint16_t color);

// Header-bar name block: a filled rounded rectangle (`fillRoundRect`,
// corner radius `cornerRadius`) with `label` centered inside in small
// bold text. Screen chrome uses this on the left of the header in
// `LCARS_ORANGE` with the active screen name (`FLIGHTS` / `FLIGHT` /
// `RADAR`) — see CLAUDE.md "Screen chrome". Also serves the bottom-nav
// segment pills.
void drawHeaderBlock(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t cornerRadius,
                     uint16_t fillColor, uint16_t textColor, const char *label);

// The persistent bottom navigation bar (present on all three screens -
// CLAUDE.md "Screen navigation"): `kScreenCount` equal-width segments, the
// active one highlighted. Segment geometry comes straight from
// screen_nav's bottomNavSegment(), so the drawn indicators line up exactly
// with navHitTest()'s JumpTo hit zones. Each segment gets a centred pill:
// the active screen a wide filled `LCARS_ORANGE` bar, the rest a short
// `LCARS_CYAN` outline dot. Repaints its own strip first, so it's safe to
// call every frame.
void drawBottomNav(LGFX &gfx, int activeIndex, int16_t screenWidth, int16_t screenHeight);

#endif  // ARDUINO
