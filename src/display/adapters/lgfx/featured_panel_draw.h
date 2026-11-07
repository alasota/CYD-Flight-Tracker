// featured_panel_draw — the LovyanGFX half of featured_panel: Screen 1's
// spotlight on the nearest aircraft (identity, route, stat pills) and its
// empty placeholder. Hardware adapter, not covered by Unity (CLAUDE.md
// "Testing").
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/featured_panel.h"

// Draws the featured panel for `row` across a `screenWidth`-px display, at
// the fixed y:18..95 bounds (no frame): the identity line + "WAW (PL) ->
// FCO (IT)" route line (RouteFormat::WithCountry, airports from the row),
// and the stat pills from featuredPillTexts().
void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, int16_t screenWidth);

// Draws the empty placeholder in the same bounds: "no aircraft in range",
// or why there is no data (emptyStateText(health)).
void drawFeaturedPanelEmpty(LGFX &gfx, int16_t screenWidth, FeedHealth health);
