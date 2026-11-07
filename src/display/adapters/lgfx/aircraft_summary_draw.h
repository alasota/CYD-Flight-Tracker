// aircraft_summary_draw — the LovyanGFX half of aircraft_summary: the identity
// line (text + phase icon) and the two-line identity block shared by Screen 1's
// featured panel and Screen 2. Hardware adapter, not covered by Unity
// (CLAUDE.md "Testing").
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/aircraft_summary.h"

// Draws the two-line identity block (identity line + route line) into
// [x, y, w, h], for Screen 1's featured panel and Screen 2. Uses
// lcars_theme fonts/colors, no chrome of its own — the caller draws any
// surrounding panel/frame. The route line uses city names
// (formatRowRouteCities(), "Warszawa (PL) -> Rzym (IT)").
void drawAircraftSummary(LGFX &gfx, const TrackedFlight &row, int16_t x, int16_t y, int16_t w,
                         int16_t h);

// The identity line alone, top-left at (x, y): fitSummaryIdentity() text
// on the left, the phase icon right-aligned so its right edge sits at
// `rightX`. Nothing is drawn past `rightX`. Shared by drawAircraftSummary()
// and featured_panel so both obey the same overflow rule.
void drawIdentityLine(LGFX &gfx, const TrackedFlight &row, int16_t x, int16_t y, int16_t rightX);
