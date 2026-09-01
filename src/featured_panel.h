// featured_panel — Screen 1's top zone (y:28..105), spotlighting the
// closest aircraft inside an LCARS elbow frame (LCARS_MAGENTA, swept
// top-left corner — lcars_theme::drawElbowFrame). The identity text comes
// from aircraft_summary (identity line + WithCountry route line); this
// module adds only its own altitude / speed / distance stat chips and the
// "no aircraft in range" placeholder. See CLAUDE.md "Screen 1 — Flights".
//
// Zero networking code — draws from an already-enriched AircraftRow
// (table_view's type) plus the two AirportInfo results for its route's
// endpoints, looked up separately for only the one featured row.
#pragma once

#include <cstdint>

#include "aircraft_summary.h"  // RouteFormat, the identity/route formatters
#include "lcars_theme.h"
#include "route_lookup.h"  // AirportInfo

// Forward-declared rather than #include "table_view.h" (which includes
// this header — cycle). featured_panel.cpp includes table_view.h for the
// full definition. Same pattern as aircraft_summary.h.
struct AircraftRow;

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed vertical bounds of the featured panel on the 320x240 frame, per
// CLAUDE.md "Screen 1" ("Featured flight section, y: 28 to 105px").
// table_view starts its table section immediately below.
int16_t featuredPanelTopPx();     // 28
int16_t featuredPanelHeightPx();  // 77 (28..105)

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// aircraft_summary. Not covered by Unity (see CLAUDE.md "Testing").
// Guarded here in the header too since these signatures need the LGFX
// device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the featured panel for `row` across a `screenWidth`-px display, at
// the fixed y:28..105 bounds: the LCARS_MAGENTA elbow frame, the identity
// line + "WAW (PL) -> FCO (IT)" route line (via aircraft_summary in
// RouteFormat::WithCountry mode), and altitude/speed/distance as
// lcars_theme pills. `originAirport`/`destAirport` are the row's route
// endpoints (pass default-constructed AirportInfo{} if not resolved).
void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                       const AirportInfo &destAirport, int16_t screenWidth);

// Draws the "no aircraft in range" placeholder in the same bounds.
void drawFeaturedPanelEmpty(LGFX &gfx, int16_t screenWidth);

#endif  // ARDUINO
