// lcars_theme — shared LCARS-inspired color palette, panel/elbow/pill
// drawing helpers, and font choices. See CLAUDE.md "Design language". This
// is the only place in the project that defines colors/shapes/fonts —
// table_view and (later) radar_view call into this rather than picking
// their own.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "flight_phase.h"  // Phase — for phaseIcon()
#include "i18n.h"          // Language, tr() — bottom-nav labels
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
constexpr uint16_t LCARS_ORANGE = 0xFC00;   // (original palette — no longer used on screen)
constexpr uint16_t LCARS_MAGENTA = 0xF81F;  // (original palette — no longer used on screen)
constexpr uint16_t LCARS_CYAN = 0x07FF;     // (original palette — no longer used on screen)
constexpr uint16_t LCARS_YELLOW = 0xFFE0;   // radar home crosshair, "imminent" countdown, rate-limit

// Colour scheme "variant A" (CLAUDE.md "Design language"): warm chrome
// (header + bottom-nav buttons) vs. cool content (flight data, tables,
// radar), so navigation and data never look alike.
constexpr uint16_t LCARS_PEACH = rgb565(0xFF, 0x99, 0x66);        // 0xFCCC
constexpr uint16_t LCARS_PEACH_LIGHT = rgb565(0xFF, 0xCC, 0x99);  // 0xFE73
constexpr uint16_t LCARS_LAVENDER = rgb565(0x99, 0x77, 0xAA);     // 0x9BB5
constexpr uint16_t LCARS_LILAC = rgb565(0xCC, 0x99, 0xCC);        // 0xCCD9
constexpr uint16_t LCARS_SKY = rgb565(0x99, 0xCC, 0xFF);          // 0x9E7F
constexpr uint16_t LCARS_NAVY = rgb565(0x10, 0x20, 0x40);         // 0x1108
constexpr uint16_t LCARS_GREEN = rgb565(0x66, 0xCC, 0x66);        // 0x666C
constexpr uint16_t LCARS_RED = rgb565(0xFF, 0x33, 0x33);          // 0xF986
constexpr uint16_t LCARS_WHITE = 0xFFFF;

// Roles — what the view modules actually use, so a future re-colour is an
// edit here, not a hunt through every screen.
// Chrome (header bar + bottom nav):
constexpr uint16_t THEME_CHROME_ACCENT = LCARS_PEACH;        // header block (data OK), active button
constexpr uint16_t THEME_CHROME_TEXT = LCARS_PEACH_LIGHT;    // header date / time
constexpr uint16_t THEME_CHROME_INACTIVE = LCARS_LAVENDER;   // inactive nav buttons
constexpr uint16_t THEME_ON_FILL = LCARS_BLACK;              // text on any filled chrome/table bar
constexpr uint16_t THEME_ALERT = LCARS_RED;                  // header block: auth/network error
// Content (flight data):
constexpr uint16_t THEME_CONTENT_PRIMARY = LCARS_WHITE;      // callsigns, odd table rows
constexpr uint16_t THEME_CONTENT_SECONDARY = LCARS_SKY;      // airline / type / route, even rows
constexpr uint16_t THEME_CONTENT_MUTED = LCARS_LAVENDER;     // radar rings & labels, "no phase"
constexpr uint16_t THEME_PILL_FILL = LCARS_NAVY;             // altitude/speed/distance chips
constexpr uint16_t THEME_TABLE_HEADER = LCARS_LILAC;         // table column-header bar

// Shared header-bar height (see CLAUDE.md "Screen chrome"): status_bar
// draws y:0..LCARS_HEADER_HEIGHT — a slim 15px strip (Font0 text).
constexpr int16_t LCARS_HEADER_HEIGHT = 15;

// Height of the persistent bottom nav bar (CLAUDE.md "Screen navigation"):
// big 25px buttons (Font2 labels), drawn at y:215..240. (screen_nav gives
// its touch hit-zone a little more vertical slack than that.)
constexpr int16_t LCARS_BOTTOM_NAV_HEIGHT = 25;

constexpr int16_t LCARS_SCREEN_HEIGHT = 240;  // landscape panel height

// The content area every screen draws into: y:18..215 (197px). All screen
// layouts are expressed relative to these two, so resizing the header or
// the nav bar moves every screen with it instead of leaving stale
// hardcoded y values behind.
constexpr int16_t LCARS_CONTENT_TOP = LCARS_HEADER_HEIGHT + 3;  // 3px gap below the header
constexpr int16_t LCARS_CONTENT_BOTTOM = LCARS_SCREEN_HEIGHT - LCARS_BOTTOM_NAV_HEIGHT;

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

// Colour for a phase icon — takeoff green, landing peach, overflight sky,
// no phase muted lavender — so takeoff/landing stand out from plain text.
// Pure — native-tested.
uint16_t phaseColor(Phase phase);

// The drawn phase icon (replaces the old ASCII ^ v > glyphs, which were too
// small to read): a filled triangle fitting a `size` x `size` box centred on
// (cx, cy) — ▲ takeoff (apex x0,y0 on top), ▼ landing (apex at the bottom),
// ▶ overflight (tip x0,y0 on the right); `visible` is false for NONE (no
// icon at all). Pure geometry — native-tested; drawPhaseIcon() fills it.
struct PhaseTriangle {
  bool visible = false;
  int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
PhaseTriangle phaseTriangle(Phase phase, int16_t cx, int16_t cy, int16_t size);

// Text colour for table row `rowIndex` (0-based within the page): white /
// sky alternating, so a row is easy to follow across the columns. Pure.
uint16_t tableRowTextColor(int rowIndex);

// Rendered pixel width of a string in whatever font is current. On the
// device this wraps gfx.textWidth(); tests pass a fixed-pitch fake.
using TextMeasure = std::function<int(const std::string &)>;

// `text` cut down so that measure(result) <= maxPx — the one truncation
// helper for every place a string must not spill out of its cell/panel
// (CLAUDE.md "Screen 1": table cells, the identity line, Screen 3's
// airline row):
//   - already fits                -> returned unchanged.
//   - otherwise                   -> longest head + "..." that fits, with
//     trailing spaces trimmed off the head ("LOT..." not "LOT ...").
//     ASCII "...", not "…" — bitmap fonts only cover 0x20-0x7E.
//   - not even "X..." fits        -> longest plain head that fits.
//   - maxPx <= 0 / nothing fits   -> "".
std::string fitToWidth(const std::string &text, int maxPx, const TextMeasure &measure);

// Glyph height of LCARS_FONT_SMALL (fonts::Font0, the 6x8 GLCD font) —
// exposed for the native test that it fits the 15px header.
constexpr int16_t LCARS_FONT_SMALL_HEIGHT_PX = 8;

// Glyph height of LCARS_FONT_BODY (fonts::Font2) — for the native test
// that bottom-nav labels fit the 25px buttons.
constexpr int16_t LCARS_FONT_BODY_HEIGHT_PX = 16;

// Side of the drawn phase triangle (drawPhaseIcon()) — a bit smaller than
// the 16px text line it sits next to.
constexpr int16_t kPhaseIconSize = 10;

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
extern const lgfx::IFont *const LCARS_FONT_SMALL;    // 8px labels for thin strips (sub-header, column header)

// Adapter: a TextMeasure that measures with `gfx`'s *current* font/size.
TextMeasure gfxTextMeasure(lgfx::LovyanGFX &gfx);

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

// Fills phaseTriangle(phase, cx, cy, size) in phaseColor(phase). Draws
// nothing for Phase::NONE.
void drawPhaseIcon(LGFX &gfx, Phase phase, int16_t cx, int16_t cy, int16_t size);

// Pill-shaped button/label: a fully rounded rectangle (corner radius =
// h/2) with text centered inside. Used for the stat pills.
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
// corner radius `cornerRadius`) with `label` centered inside in
// LCARS_FONT_SMALL (8px — the header is only 15px tall). Screen chrome uses
// this on the left of the header for the aircraft count, coloured by feed
// health — see CLAUDE.md "Screen chrome".
void drawHeaderBlock(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t cornerRadius,
                     uint16_t fillColor, uint16_t textColor, const char *label);

// The persistent bottom navigation bar (present on all three screens -
// CLAUDE.md "Screen navigation"): `kScreenCount` equal-width segments, the
// active one highlighted. Segment geometry comes straight from
// screen_nav's bottomNavSegment(), so the drawn indicators line up exactly
// with navHitTest()'s JumpTo hit zones. Each segment is a labelled pill
// button filling the bar's height (label from `labelFor(index, i18nLanguage())`, in
// LCARS_FONT_BODY on 25px buttons): the active screen filled `LCARS_ORANGE` with black
// text, the others dim with a `LCARS_CYAN` outline and cyan text. (The
// first version drew 6px-tall dots, which were too small to see on the
// device.) Repaints its own strip first, so it's safe to call every frame.
void drawBottomNav(LGFX &gfx, int activeIndex, int16_t screenWidth, int16_t screenHeight,
                   const char *(*labelFor)(int screenIndex, Language lang));

#endif  // ARDUINO
