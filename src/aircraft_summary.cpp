#include "aircraft_summary.h"

#include "table_view.h"  // full AircraftRow definition

namespace {

std::string orDash(const std::string &s) { return s.empty() ? "--" : s; }

// One route endpoint, formatted per `fmt`. `airport` may be unresolved
// (found == false) — then fall back to the bare ICAO code, never a
// country.
std::string endpointLabel(const std::string &icao, const AirportInfo &airport, RouteFormat fmt) {
  if (!airport.found || airport.iata_code.empty()) {
    return icao;
  }
  if (fmt == RouteFormat::WithCountry) {
    return airport.iata_code + " (" + airport.country_code + ")";
  }
  return airport.iata_code;
}

}  // namespace

std::string summaryFlight(const AircraftRow &row) { return orDash(row.aircraft.callsign); }

std::string summaryAirline(const AircraftRow &row) {
  return row.info.found ? orDash(row.info.airline) : "--";
}

std::string summaryType(const AircraftRow &row) {
  return row.info.found ? orDash(row.info.aircraft_type) : "--";
}

std::string formatSummaryIdentity(const AircraftRow &row) {
  std::string line = summaryFlight(row) + "  " + summaryAirline(row) + "  " + summaryType(row) + "  ";
  line += phaseIcon(row.phase);
  return line;
}

std::string formatSummaryRoute(const RouteInfo &route, const AirportInfo &originAirport,
                               const AirportInfo &destAirport, RouteFormat fmt) {
  if (!route.found) {
    return "--";
  }
  return endpointLabel(route.origin_icao, originAirport, fmt) + " -> " +
         endpointLabel(route.dest_icao, destAirport, fmt);
}

#ifdef ARDUINO

#include "lcars_theme.h"

namespace {
constexpr int16_t kTextMargin = 6;
constexpr int16_t kLineHeight = 20;
}  // namespace

void drawAircraftSummary(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                         const AirportInfo &destAirport, RouteFormat fmt, int16_t x, int16_t y,
                         int16_t w, int16_t h) {
  (void)w;
  (void)h;

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(top_left);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);

  gfx.drawString(formatSummaryIdentity(row).c_str(), static_cast<int16_t>(x + kTextMargin),
                 static_cast<int16_t>(y + kTextMargin));
  gfx.drawString(formatSummaryRoute(row.route, originAirport, destAirport, fmt).c_str(),
                 static_cast<int16_t>(x + kTextMargin),
                 static_cast<int16_t>(y + kTextMargin + kLineHeight));
}

#endif  // ARDUINO
