// flight_screen — draws Screen 2 ("Flight"): the nearest aircraft's
// identity plus one big number — how many seconds until it's overhead.
// See docs/screens.md "Screen 2 — Flight" and "Flight ETA".
//
// Three fixed zones in the content area (LCARS_CONTENT_TOP..BOTTOM, between
// the header and the bottom nav; exact y values are the kFlight* constants
// below):
//   - identity panel  — text only, no frame (as featured_panel): identity,
//                       route and stats lines via aircraft_summary
//   - countdown zone  — giant digits, colour-coded by urgency
//   - status strip    — a text label matching the countdown state
//
// Zero networking code — draws from an already-enriched AircraftRow
// (airspace/core/tracked_flight), its two route-endpoint AirportInfos, and a
// cpa_predictor::CpaPrediction. The between-polls "local ticking" of the
// countdown is the UI's job (main.cpp) — this module just renders
// whatever CpaPrediction it's handed. Layout and texts are pure; the
// LovyanGFX drawing (and the countdown sprite) is
// display/adapters/lgfx/flight_screen_draw.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "airspace/core/tracked_flight.h"  // AircraftRow
#include "display/core/screens/aircraft_summary.h"  // RouteFormat + the identity/route formatters
#include "airspace/core/cpa_predictor.h"     // CpaPrediction
#include "display/core/i18n.h"              // Language, tr()
#include "display/core/theme/lcars_theme.h"       // palette constants (pure section)
#include "enrichment/core/route_lookup.h"      // AirportInfo
#include "display/core/screens/status_bar.h"        // OpenSkyHealth, emptyStateText()

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed zone geometry from docs/screens.md "Screen 2" — absolute y on the
// 320x240 frame; the constants for the drawing, the functions for the tests.
constexpr int16_t kFlightIdentityTop = LCARS_CONTENT_TOP;  // 18
constexpr int16_t kFlightIdentityHeight = 70;  // y:18..88 — identity, route, stats lines
constexpr int16_t kFlightCountdownTop = kFlightIdentityTop + kFlightIdentityHeight + 4;  // 92
constexpr int16_t kFlightStatusTop = LCARS_CONTENT_BOTTOM - 25;  // 190
constexpr int16_t kFlightStatusHeight = 25;  // y:190..215 (bottom nav: 215..240)
constexpr int16_t kFlightCountdownHeight =
    kFlightStatusTop - 4 - kFlightCountdownTop;  // 94 (y:92..186)
int16_t flightIdentityTopPx();      // 18
int16_t flightIdentityHeightPx();   // 70  (18..88): identity, route, stats lines
int16_t flightCountdownTopPx();     // 92
int16_t flightCountdownHeightPx();  // 94  (92..186)
int16_t flightStatusTopPx();        // 190
int16_t flightStatusHeightPx();     // 25  (190..215)

// Screen 2's stats line under the route (after the direction arrow):
// "S 3km  236m  266km/h", or "S 6km  NA ZIEMI" on the ground. Pure.
std::string nowStatsText(const AircraftRow &row, Language lang);

// How the countdown zone renders a given CpaPrediction.
struct CountdownDisplay {
  enum class Mode { Empty, Seconds, Minutes };

  Mode mode = Mode::Empty;
  std::string bigText;      // large glyphs: "45", "-3", "~4", "--"
  std::string suffixText;   // smaller trailing text: "s", "MIN", or ""
  std::string statusLabel;  // text for the status strip
  std::string caption;      // small line above the digits: "NAD TOBĄ ZA" while
                            // the CPA is still ahead (t > 0), else ""
  uint16_t color = THEME_CONTENT_SECONDARY;  // big-text + label colour
  bool colorCoded = true;  // false in the empty state ("no colour-coding")
};

// Classifies `cpa` per docs/screens.md "Screen 2" (labels shown PL / EN, from i18n):
//   not found, or t_cpa_seconds < -10  -> Empty, "--", white, not colour-coded:
//        `anyAircraft` ? "NIE PRZELECI NAD TOBĄ" | "NOT PASSING OVERHEAD"
//                      : "BRAK LOTÓW W ZASIĘGU" | "NO FLIGHTS IN RANGE"
//   t_cpa_seconds > 60                 -> Minutes
//        "~<n>" + "MIN" / "SZACOWANY CZAS" | "ESTIMATED TIME" / lilac
//   10 < t_cpa_seconds <= 60           -> Seconds, sky    / "ZBLIŻA SIĘ" | "APPROACHING"
//   0 <= t_cpa_seconds <= 10           -> Seconds, yellow / "NAD TOBĄ" | "OVERHEAD"
//   -10 <= t_cpa_seconds < 0           -> Seconds, peach  / "MINĄŁ" | "PASSED"
// The number is rounded to the nearest whole second (nearest whole minute
// in Minutes mode). Only the text depends on `lang` — mode, number and
// colour don't.
CountdownDisplay computeCountdownDisplay(const CpaPrediction &cpa, Language lang,
                                         bool anyAircraft,
                                         OpenSkyHealth health = OpenSkyHealth::Ok);
