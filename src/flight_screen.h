// flight_screen — draws Screen 2 ("Flight"): the nearest aircraft's
// identity plus one big number — how many seconds until it's overhead.
// See CLAUDE.md "Screen 2 — Flight" and "Flight ETA — closest point of
// approach".
//
// Three fixed zones on the 320x240 frame, below the 25px status_bar
// header:
//   - identity panel   y:28..78   — same LCARS_MAGENTA elbow frame as
//                                    featured_panel, text via aircraft_summary
//   - countdown zone    y:82..195  — giant digits, colour-coded by urgency
//   - status strip      y:200..225 — a text label matching the countdown state
//
// Zero networking code — draws from an already-enriched AircraftRow
// (table_view's type), its two route-endpoint AirportInfos, and a
// cpa_predictor::CpaPrediction. The between-polls "local ticking" of the
// countdown is main.cpp's job (see CLAUDE.md) — this module just renders
// whatever CpaPrediction it's handed.
#pragma once

#include <cstdint>
#include <string>

#include "aircraft_summary.h"  // RouteFormat + the identity/route formatters
#include "cpa_predictor.h"     // CpaPrediction
#include "lcars_theme.h"       // palette constants (pure section)
#include "route_lookup.h"      // AirportInfo

// Forward-declared rather than #include "table_view.h" (cycle risk) — same
// pattern as featured_panel.h / aircraft_summary.h. flight_screen.cpp
// includes table_view.h for the full definition.
struct AircraftRow;

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed zone geometry from CLAUDE.md "Screen 2" — absolute y on the
// 320x240 frame.
int16_t flightIdentityTopPx();      // 28
int16_t flightIdentityHeightPx();   // 50   (28..78)
int16_t flightCountdownTopPx();     // 82
int16_t flightCountdownHeightPx();  // 113  (82..195)
int16_t flightStatusTopPx();        // 200
int16_t flightStatusHeightPx();     // 25   (200..225)

// How the countdown zone renders a given CpaPrediction.
struct CountdownDisplay {
  enum class Mode { Empty, Seconds, Minutes };

  Mode mode = Mode::Empty;
  std::string bigText;      // large glyphs: "45", "-3", "~4", "--"
  std::string suffixText;   // smaller trailing text: "s", "MIN", or ""
  std::string statusLabel;  // text for the y:200 status strip
  uint16_t color = LCARS_CYAN;  // big-text + label colour
  bool colorCoded = true;       // false in the empty state ("no colour-coding")
};

// Classifies `cpa` per CLAUDE.md "Screen 2":
//   not found, or t_cpa_seconds < -10  -> Empty
//        "--" / "BRAK LOTOW W ZASIEGU" / white, not colour-coded
//   t_cpa_seconds > 60                 -> Minutes
//        "~<n>" + "MIN" / "SZACOWANY CZAS" / LCARS_MAGENTA
//   10 < t_cpa_seconds <= 60           -> Seconds, LCARS_CYAN   / "ZBLIZA SIE"
//   0 <= t_cpa_seconds <= 10           -> Seconds, LCARS_YELLOW / "NAD TOBA"
//   -10 <= t_cpa_seconds < 0           -> Seconds, LCARS_ORANGE / "MINAL"
// The number is rounded to the nearest whole second (nearest whole minute
// in Minutes mode). Polish labels are ASCII-folded (no ogonek/kreska) —
// the bitmap fonts don't carry the diacritics, same rule as table_view's
// column header.
CountdownDisplay computeCountdownDisplay(const CpaPrediction &cpa);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// aircraft_summary. Not covered by Unity (see CLAUDE.md "Testing").
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws all three zones for a `screenWidth`-px display. `hasNearest` ==
// false (no aircraft in range) draws the identity frame empty and forces
// the countdown/status into their empty state regardless of `cpa`.
// `origin`/`dest` are the nearest aircraft's route endpoints (pass
// AirportInfo{} if unresolved).
void drawFlightScreen(LGFX &gfx, const AircraftRow &nearest, bool hasNearest,
                      const AirportInfo &origin, const AirportInfo &dest, const CpaPrediction &cpa,
                      int16_t screenWidth);

#endif  // ARDUINO
