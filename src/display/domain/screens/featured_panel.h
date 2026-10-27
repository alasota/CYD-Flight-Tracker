// featured_panel — Screen 1's top zone (y:18..95), spotlighting the
// nearest airborne aircraft; text and stat chips on black, no frame. The
// identity text comes from aircraft_summary (identity line + route line); this
// module adds only its own altitude / speed / distance stat chips and the
// "no aircraft in range" placeholder. See docs/screens.md "Screen 1 — Flights".
//
// Zero networking code — draws from an already-enriched AircraftRow
// (table_view's type) plus the two AirportInfo results for its route's
// endpoints, looked up separately for only the one featured row.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "display/domain/screens/aircraft_summary.h"  // RouteFormat, the identity/route formatters
#include "display/domain/theme/lcars_theme.h"
#include "enrichment/domain/route_lookup.h"  // AirportInfo
#include "display/domain/screens/status_bar.h"    // OpenSkyHealth, emptyStateText()

// Forward-declared rather than #include "table_view.h" (which includes
// this header — cycle). featured_panel.cpp includes table_view.h for the
// full definition. Same pattern as aircraft_summary.h.
struct AircraftRow;

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed vertical bounds of the featured panel on the 320x240 frame, per
// docs/screens.md "Screen 1" (featured flight section, y:18..95).
// table_view starts its table section immediately below.
int16_t featuredPanelTopPx();     // 18
int16_t featuredPanelHeightPx();  // 77 (18..95)

// Texts for the stat pills under the featured aircraft: airborne ->
// {"<alt>m", "<speed>km/h", "<compass> <dist>km"}; on the ground -> {"NA ZIEMI" /
// "ON GROUND", "<dist>km"} (altitude and speed mean nothing while taxiing,
// and "0m" / "1m/s" read like a bug — the first pill is drawn double-width).
// A missing value is "--", never a fake 0. Pure — native-tested.
std::vector<std::string> featuredPillTexts(const AircraftRow &row, Language lang);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// aircraft_summary. Not covered by Unity (see CLAUDE.md "Testing").
// Guarded here in the header too since these signatures need the LGFX
// device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the featured panel for `row` across a `screenWidth`-px display, at
// the fixed y:18..95 bounds (no frame): the identity line + "WAW (PL) ->
// FCO (IT)" route line (RouteFormat::WithCountry, airports from the row),
// and the stat pills from featuredPillTexts().
void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, int16_t screenWidth);

// Draws the empty placeholder in the same bounds: "no aircraft in range",
// or why there is no data (emptyStateText(health)).
void drawFeaturedPanelEmpty(LGFX &gfx, int16_t screenWidth, OpenSkyHealth health);

#endif  // ARDUINO
