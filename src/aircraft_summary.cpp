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

std::string fitSummaryIdentity(const AircraftRow &row, int maxPx, const TextMeasure &measure) {
  static const std::string kGap = "  ";
  const std::string flight = summaryFlight(row);
  const std::string airline = summaryAirline(row);
  const std::string type = summaryType(row);

  const std::string full = flight + kGap + airline + kGap + type;
  if (measure(full) <= maxPx) return full;

  // Airline cut to whatever's left — but only if at least "X..." fits.
  const int airlineBudget = maxPx - measure(flight + kGap) - measure(kGap + type);
  if (airlineBudget >= measure(airline.substr(0, 1) + "...")) {
    return flight + kGap + fitToWidth(airline, airlineBudget, measure) + kGap + type;
  }

  // No room for the airline at all.
  const std::string noAirline = flight + kGap + type;
  if (measure(noAirline) <= maxPx) return noAirline;

  const std::string cutType = fitToWidth(type, maxPx - measure(flight + kGap), measure);
  return cutType.empty() ? flight : flight + kGap + cutType;
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
constexpr int16_t kIconGap = 6;  // min space between identity text and phase icon
}  // namespace

void drawIdentityLine(LGFX &gfx, const AircraftRow &row, int16_t x, int16_t y, int16_t rightX) {
  gfx.setFont(LCARS_FONT_BODY);

  // Phase icon first, pinned to the right edge, in its phase colour.
  const char icon[2] = {phaseIcon(row.phase), '\0'};
  gfx.setTextColor(phaseColor(row.phase), LCARS_BLACK);
  gfx.setTextDatum(top_right);
  gfx.drawString(icon, rightX, y);
  const int iconW = static_cast<int>(gfx.textWidth(icon));

  // Then the identity text, fitted into what's left: airline/type in the
  // secondary colour, then the callsign re-drawn over its own prefix in
  // the primary colour (bitmap font, no kerning — the glyphs land on
  // exactly the same pixels, so this just recolours it).
  const int maxPx = (rightX - iconW - kIconGap) - x;
  gfx.setTextDatum(top_left);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.drawString(fitSummaryIdentity(row, maxPx, gfxTextMeasure(gfx)).c_str(), x, y);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(summaryFlight(row).c_str(), x, y);
}

void drawAircraftSummary(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                         const AirportInfo &destAirport, RouteFormat fmt, int16_t x, int16_t y,
                         int16_t w, int16_t h) {
  (void)h;

  const int16_t textX = static_cast<int16_t>(x + kTextMargin);
  const int16_t rightX = static_cast<int16_t>(x + w - kTextMargin);

  drawIdentityLine(gfx, row, textX, static_cast<int16_t>(y + kTextMargin), rightX);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(top_left);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.drawString(
      fitToWidth(formatSummaryRoute(row.route, originAirport, destAirport, fmt), rightX - textX,
                 gfxTextMeasure(gfx))
          .c_str(),
      textX, static_cast<int16_t>(y + kTextMargin + kLineHeight));
}

#endif  // ARDUINO
