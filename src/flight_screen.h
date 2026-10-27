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
#include <vector>

#include "aircraft_summary.h"  // RouteFormat + the identity/route formatters
#include "cpa_predictor.h"     // CpaPrediction
#include "i18n.h"              // Language, tr()
#include "lcars_theme.h"       // palette constants (pure section)
#include "route_lookup.h"      // AirportInfo
#include "status_bar.h"        // OpenSkyHealth, emptyStateText()

// Forward-declared rather than #include "table_view.h" (cycle risk) — same
// pattern as featured_panel.h / aircraft_summary.h. flight_screen.cpp
// includes table_view.h for the full definition.
struct AircraftRow;

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Fixed zone geometry from CLAUDE.md "Screen 2" — absolute y on the
// 320x240 frame.
int16_t flightIdentityTopPx();      // 18
int16_t flightIdentityHeightPx();   // 70  (18..88): identity, route, stats lines
int16_t flightCountdownTopPx();     // 92
int16_t flightCountdownHeightPx();  // 94  (92..186)
int16_t flightStatusTopPx();        // 190
int16_t flightStatusHeightPx();     // 25  (190..215)

// "Will pass overhead" = the closest point of approach is within this many
// km of home.
constexpr float kOverheadRadiusKm = 5.0f;

// Which aircraft Screen 2 shows, and its countdown.
struct NowPick {
  int index = -1;     // into the rows; -1 = no aircraft at all
  CpaPrediction cpa;  // found == false -> nobody will pass overhead
};

// Screen 2's job is "when will it be overhead", so it picks — among
// airborne aircraft whose CPA is at most `overheadKm` from home and not
// more than 10 s in the past — the one with the *soonest* CPA (not simply
// the nearest one). Without such a candidate it shows the nearest airborne
// aircraft (rows are nearest-first) with no prediction, or row 0 if all are
// on the ground. `cpas[i]` belongs to `rows[i]`. Pure — native-tested.
NowPick pickNowAircraft(const std::vector<AircraftRow> &rows,
                        const std::vector<CpaPrediction> &cpas, float overheadKm);

// Screen 2's stats line under the route (after the direction arrow):
// "S 3km  236m  266km/h", or "S 6km  NA ZIEMI" on the ground. Pure.
std::string nowStatsText(const AircraftRow &row, Language lang);

// How the countdown zone renders a given CpaPrediction.
struct CountdownDisplay {
  enum class Mode { Empty, Seconds, Minutes };

  Mode mode = Mode::Empty;
  std::string bigText;      // large glyphs: "45", "-3", "~4", "--"
  std::string suffixText;   // smaller trailing text: "s", "MIN", or ""
  std::string statusLabel;  // text for the y:200 status strip
  std::string caption;      // small line above the digits: "NAD TOBĄ ZA" while
                            // the CPA is still ahead (t > 0), else ""
  uint16_t color = THEME_CONTENT_SECONDARY;  // big-text + label colour
  bool colorCoded = true;       // false in the empty state ("no colour-coding")
};

// Classifies `cpa` per CLAUDE.md "Screen 2" (labels shown PL / EN, from i18n):
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

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// aircraft_summary. Not covered by Unity (see CLAUDE.md "Testing").
#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Draws all three zones for a `screenWidth`-px display. `hasNearest` ==
// false (no aircraft in range) draws the identity frame empty and forces
// the countdown/status into their empty state regardless of `cpa`.
void drawFlightScreen(LGFX &gfx, const AircraftRow &nearest, bool hasNearest,
                      const CpaPrediction &cpa, int16_t screenWidth, OpenSkyHealth health);

// Repaints only the countdown zone + status strip (y:82..225) — the 1 Hz
// tick path (CLAUDE.md "Redraw policy"), leaving the identity panel,
// header and bottom nav untouched. Renders into an off-screen 8-bit
// LGFX_Sprite and pushes it in one go so the digits don't flicker; if the
// sprite can't be allocated (low heap) it falls back to drawing straight
// to `gfx`. drawFlightScreen() calls this for its lower two zones.
void drawFlightCountdown(LGFX &gfx, bool hasNearest, const CpaPrediction &cpa,
                         int16_t screenWidth, OpenSkyHealth health);

// Allocates the countdown sprite now (setup(), before WiFi / net_task) so it
// doesn't later split the largest free heap block. Safe to call again.
void flightScreenReserveSprite(int16_t screenWidth);

#endif  // ARDUINO
