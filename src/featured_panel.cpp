#include "featured_panel.h"

#include "table_view.h"  // full AircraftRow definition

namespace {
constexpr int16_t kPanelTop = 28;
constexpr int16_t kPanelHeight = 77;  // y:28..105 per CLAUDE.md "Screen 1"
}  // namespace

int16_t featuredPanelTopPx() { return kPanelTop; }
int16_t featuredPanelHeightPx() { return kPanelHeight; }

#ifdef ARDUINO

#include <cstdio>

namespace {
constexpr int16_t kSideMargin = 4;     // frame inset from the screen edges
constexpr int16_t kFrameCorner = 14;   // swept top-left corner radius
constexpr int16_t kFrameThickness = 3;
constexpr int16_t kIdentityY = 34;     // identity line, clears the swept corner
constexpr int16_t kRouteY = 56;        // "WAW (PL) -> FCO (IT)" line
constexpr int16_t kPillY = 78;         // altitude / speed / distance chips
constexpr int16_t kPillHeight = 22;
constexpr int16_t kPillGap = 4;
}  // namespace

void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                       const AirportInfo &destAirport, int16_t screenWidth) {
  int16_t w = static_cast<int16_t>(screenWidth - 2 * kSideMargin);

  drawElbowFrame(gfx, kSideMargin, kPanelTop, w, kPanelHeight, kFrameCorner, kFrameThickness,
                 LCARS_MAGENTA);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);

  // Identity line — indented past the swept corner.
  gfx.setTextDatum(top_left);
  gfx.drawString(formatSummaryIdentity(row).c_str(),
                 static_cast<int16_t>(kSideMargin + kFrameCorner + 4), kIdentityY);

  // Route line, with country codes (Screen 1 detail level).
  gfx.drawString(
      formatSummaryRoute(row.route, originAirport, destAirport, RouteFormat::WithCountry).c_str(),
      static_cast<int16_t>(kSideMargin + kFrameThickness + 4), kRouteY);

  // Altitude / speed / distance pills along the bottom of the frame.
  int16_t pillAreaX = static_cast<int16_t>(kSideMargin + kFrameThickness + 2);
  int16_t pillAreaW = static_cast<int16_t>(w - 2 * (kFrameThickness + 2));
  int16_t pillW = static_cast<int16_t>((pillAreaW - 2 * kPillGap) / 3);

  char buf[16];

  std::snprintf(buf, sizeof(buf), "%.0fm",
                row.aircraft.has_position ? static_cast<double>(row.aircraft.baro_altitude) : 0.0);
  drawPillButton(gfx, pillAreaX, kPillY, pillW, kPillHeight, LCARS_BLUE_VIOLET, LCARS_BLACK, buf);

  std::snprintf(buf, sizeof(buf), "%.0fm/s",
                row.aircraft.has_position ? static_cast<double>(row.aircraft.velocity) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(pillAreaX + pillW + kPillGap), kPillY, pillW, kPillHeight,
                 LCARS_BLUE_VIOLET, LCARS_BLACK, buf);

  std::snprintf(buf, sizeof(buf), "%.0fkm",
                row.has_distance ? static_cast<double>(row.distance_km) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(pillAreaX + 2 * (pillW + kPillGap)), kPillY, pillW,
                 kPillHeight, LCARS_BLUE_VIOLET, LCARS_BLACK, buf);
}

void drawFeaturedPanelEmpty(LGFX &gfx, int16_t screenWidth) {
  int16_t w = static_cast<int16_t>(screenWidth - 2 * kSideMargin);

  drawElbowFrame(gfx, kSideMargin, kPanelTop, w, kPanelHeight, kFrameCorner, kFrameThickness,
                 LCARS_MAGENTA);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
  gfx.drawString("No aircraft in range", static_cast<int16_t>(screenWidth / 2),
                 static_cast<int16_t>(kPanelTop + kPanelHeight / 2));
}

#endif  // ARDUINO
