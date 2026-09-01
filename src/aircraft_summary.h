// aircraft_summary — the shared aircraft identity block (flight / airline /
// aircraft type / phase icon, plus origin -> destination) rendered the
// same way on all three screens, per CLAUDE.md "Code conventions"
// (aircraft_summary) and "Visualization concept". Centralized here so this
// isn't copy-pasted into featured_panel (Screen 1), flight_screen
// (Screen 2) and radar_view (Screen 3).
//
// The one thing that differs between screens is how much of the route to
// show — Screen 1 & 2 want country codes ("WAW (PL) -> FCO (IT)"),
// Screen 3's narrower panel wants bare codes ("WAW -> FCO"). That's a
// RouteFormat parameter, not three separate functions.
//
// Zero networking code — draws from an already-enriched AircraftRow plus
// the two AirportInfo results for its route endpoints (looked up
// elsewhere, exactly as featured_panel already receives them).
#pragma once

#include <string>

#include "route_lookup.h"  // RouteInfo, AirportInfo

// Forward-declared rather than #include "table_view.h" (which pulls in a
// lot and would risk an include cycle) — only a reference to AircraftRow
// is needed in these signatures. aircraft_summary.cpp includes
// table_view.h itself for the full definition. Same pattern as
// featured_panel.h.
struct AircraftRow;

// How the origin -> destination line is rendered.
enum class RouteFormat {
  WithCountry,  // "WAW (PL) -> FCO (IT)"  — Screen 1 (featured) & Screen 2
  CodesOnly,    // "WAW -> FCO"            — Screen 3's narrow summary panel
};

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Individual identity fields, each falling back to "--" when the
// underlying lookup hasn't resolved (info.found == false) or the field is
// empty — same "show the row anyway as '—'" rule the rest of the UI uses.
std::string summaryFlight(const AircraftRow &row);   // callsign
std::string summaryAirline(const AircraftRow &row);  // RegisteredOwners
std::string summaryType(const AircraftRow &row);     // aircraft type

// "<flight>  <airline>  <type>  <phase icon>" — the one-line identity used
// by Screen 1's featured panel and Screen 2. Screen 3 draws its three
// fields on separate rows and calls the field helpers above instead.
std::string formatSummaryIdentity(const AircraftRow &row);

// "<origin> -> <dest>", each end formatted per `fmt`:
//   WithCountry: "<IATA> (<country>)", e.g. "WAW (PL)"
//   CodesOnly:   "<IATA>",             e.g. "WAW"
// If `route` hasn't resolved (RouteInfo::found == false) the whole line is
// "--". If a route resolved but that end's airport lookup didn't, that end
// falls back to its bare ICAO code (from RouteInfo) with no country — an
// unresolved airport shouldn't hide an otherwise-known route.
std::string formatSummaryRoute(const RouteInfo &route, const AirportInfo &originAirport,
                               const AirportInfo &destAirport, RouteFormat fmt);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme. Not
// covered by Unity (see CLAUDE.md "Testing"). Guarded here in the header
// too since the signature needs the LGFX device type.
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws the two-line identity block (identity line + route line) into
// [x, y, w, h], for Screen 1's featured panel and Screen 2. Uses
// lcars_theme fonts/colors, no chrome of its own — the caller draws any
// surrounding panel/frame. `fmt` selects the route line's detail level.
void drawAircraftSummary(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                         const AirportInfo &destAirport, RouteFormat fmt, int16_t x, int16_t y,
                         int16_t w, int16_t h);

#endif  // ARDUINO
