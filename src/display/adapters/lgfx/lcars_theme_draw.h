// lcars_theme_draw — the LovyanGFX half of lcars_theme: the project's fonts
// (efont subsets with the Polish letters, Font4, DejaVu12) and the shared
// drawing helpers every screen uses — phase icons, the direction arrow, pills
// and the bottom nav. Hardware adapter, not covered by Unity (CLAUDE.md
// "Testing").
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/text/fit_to_width.h"  // TextMeasure — gfxTextMeasure()
#include "display/core/theme/lcars_theme.h"

// Fonts used across the project, chosen once here rather than per-view —
// table_view/radar_view reference these instead of picking their own.
extern const lgfx::IFont *const LCARS_FONT_HEADING;  // panel/header titles
extern const lgfx::IFont *const LCARS_FONT_BODY;     // table rows, labels
extern const lgfx::IFont *const LCARS_FONT_SMALL;    // 10px labels for thin strips (header, column header)
extern const lgfx::IFont *const LCARS_FONT_TABLE;    // table rows: efont 14px (Polish letters)
extern const lgfx::IFont *const LCARS_FONT_CLOCK;    // header clock: DejaVu12 (13px line, ASCII digits only)

// Adapter: a TextMeasure that measures with `gfx`'s *current* font/size.
TextMeasure gfxTextMeasure(lgfx::LovyanGFX &gfx);

// Draws phaseIconRows(kind) centred on (cx, cy), one fastHLine per run of
// same-coloured pixels. Draws nothing for PhaseIconKind::NONE.
void drawPhaseIcon(LGFX &gfx, PhaseIconKind kind, int16_t cx, int16_t cy);

// Fills directionArrow(bearingDeg, cx, cy, size) in `color` — the "where to
// look" arrow next to distances (featured panel, Screen 2).
void drawDirectionArrow(LGFX &gfx, float bearingDeg, int16_t cx, int16_t cy, int16_t size,
                        uint16_t color);

// Pill-shaped button/label: a fully rounded rectangle (corner radius =
// h/2) with text centered inside. Used for the stat pills.
void drawPillButton(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fillColor,
                     uint16_t textColor, const char *text);

// The persistent bottom navigation bar (present on all three screens -
// docs/screens.md "Screen navigation"): `kScreenCount` equal-width segments, the
// active one highlighted. Segment geometry comes straight from
// screen_nav's bottomNavSegment(), so the drawn indicators line up exactly
// with navHitTest()'s JumpTo hit zones. Drawn as folder tabs hanging from a
// peach rule along the bar's top edge (geometry: screen_nav's navTabRule() /
// navTabRect()): the active tab is peach, full height, merged into the rule,
// black label; inactive tabs are recessed (dark fill, lavender outline and
// label, a few px lower). Square top corners, rounded bottom ones. Labels
// from `labelFor(index, i18nLanguage())` in LCARS_FONT_BODY. Repaints its
// own strip first, so it's safe to call every frame.
void drawBottomNav(LGFX &gfx, int activeIndex, int16_t screenWidth, int16_t screenHeight,
                   const char *(*labelFor)(int screenIndex, Language lang));
