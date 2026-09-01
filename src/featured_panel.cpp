#include "featured_panel.h"

#include "table_view.h"  // full AircraftRow definition

namespace {
constexpr int16_t kPanelHeight = 74;
}  // namespace

int16_t featuredPanelHeightPx() { return kPanelHeight; }

std::string formatFeaturedLine1(const AircraftRow &row) {
  std::string flight = row.aircraft.callsign.empty() ? "--" : row.aircraft.callsign;
  std::string airline = row.info.found ? row.info.airline : "--";
  std::string type = row.info.found ? row.info.aircraft_type : "--";

  std::string line = flight + "  " + airline + "  " + type + "  ";
  line += phaseIcon(row.phase);
  return line;
}

std::string formatFeaturedLine2(const RouteInfo &route, const AirportInfo &originAirport,
                                 const AirportInfo &destAirport) {
  if (!route.found) {
    return "--";
  }

  std::string originLabel = (originAirport.found && !originAirport.iata_code.empty())
                                 ? originAirport.iata_code + " (" + originAirport.country_code + ")"
                                 : route.origin_icao;
  std::string destLabel = (destAirport.found && !destAirport.iata_code.empty())
                               ? destAirport.iata_code + " (" + destAirport.country_code + ")"
                               : route.dest_icao;

  return originLabel + " -> " + destLabel;
}

#ifdef ARDUINO

#include <cstdio>

namespace {
constexpr int16_t kLineHeight = 20;
constexpr int16_t kPillHeight = 22;
constexpr int16_t kPillGap = 4;
constexpr int16_t kTextMargin = 6;
}  // namespace

void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                        const AirportInfo &destAirport, int16_t x, int16_t y, int16_t w,
                        int16_t h) {
  drawPanel(gfx, x, y, w, h, LCARS_AMBER);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(top_left);
  gfx.setTextColor(LCARS_BLACK, LCARS_AMBER);

  std::string line1 = formatFeaturedLine1(row);
  gfx.drawString(line1.c_str(), x + kTextMargin, y + kTextMargin);

  std::string line2 = formatFeaturedLine2(row.route, originAirport, destAirport);
  gfx.drawString(line2.c_str(), x + kTextMargin, y + kTextMargin + kLineHeight);

  // Altitude / speed / distance pills along the bottom of the panel.
  int16_t pillY = static_cast<int16_t>(y + h - kPillHeight - kPillGap);
  int16_t pillW = static_cast<int16_t>((w - 4 * kPillGap) / 3);

  char buf[16];

  std::snprintf(buf, sizeof(buf), "%.0fm",
                row.aircraft.has_position ? static_cast<double>(row.aircraft.baro_altitude) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(x + kPillGap), pillY, pillW, kPillHeight,
                 LCARS_BLUE_VIOLET, LCARS_BLACK, buf);

  std::snprintf(buf, sizeof(buf), "%.0fm/s",
                row.aircraft.has_position ? static_cast<double>(row.aircraft.velocity) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(x + 2 * kPillGap + pillW), pillY, pillW, kPillHeight,
                 LCARS_BLUE_VIOLET, LCARS_BLACK, buf);

  std::snprintf(buf, sizeof(buf), "%.0fkm",
                row.has_distance ? static_cast<double>(row.distance_km) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(x + 3 * kPillGap + 2 * pillW), pillY, pillW, kPillHeight,
                 LCARS_BLUE_VIOLET, LCARS_BLACK, buf);
}

void drawFeaturedPanelEmpty(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h) {
  drawPanel(gfx, x, y, w, h, LCARS_BLACK);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(LCARS_PALE_BLUE, LCARS_BLACK);
  gfx.drawString("No aircraft in range", x + w / 2, y + h / 2);
}

#endif  // ARDUINO
