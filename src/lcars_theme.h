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

// The project's palette: black background plus 3-4 saturated accent
// colors, reused everywhere (see CLAUDE.md "Design language") rather than
// introducing new colors per element.
constexpr uint16_t LCARS_BLACK = 0x0000;
constexpr uint16_t LCARS_AMBER = rgb565(0xFF, 0x99, 0x00);        // amber/orange
constexpr uint16_t LCARS_BLUE_VIOLET = rgb565(0x99, 0x66, 0xFF);  // blue-violet
constexpr uint16_t LCARS_ROSE = rgb565(0xFF, 0x66, 0x66);         // salmon/rose
constexpr uint16_t LCARS_PALE_BLUE = rgb565(0x99, 0xCC, 0xFF);    // pale blue

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

#endif  // ARDUINO
