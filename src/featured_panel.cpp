#include "featured_panel.h"

#include "table_view.h"  // full AircraftRow definition

namespace {
constexpr int16_t kPanelTop = LCARS_CONTENT_TOP;  // 18
constexpr int16_t kPanelHeight = 77;  // y:18..95 per CLAUDE.md "Screen 1"
}  // namespace

int16_t featuredPanelTopPx() { return kPanelTop; }
int16_t featuredPanelHeightPx() { return kPanelHeight; }

#ifdef ARDUINO

#include <cstdio>

namespace {
constexpr int16_t kSideMargin = 4;     // panel inset from the screen edges
constexpr int16_t kContentInset = 3;   // extra text inset inside the panel area
constexpr int16_t kIdentityY = kPanelTop + 6;   // identity line
constexpr int16_t kRouteY = kPanelTop + 28;     // "WAW (PL) -> FCO (IT)" line
constexpr int16_t kPillY = kPanelTop + 50;      // altitude / speed / distance chips
constexpr int16_t kPillHeight = 22;
constexpr int16_t kPillGap = 4;
}  // namespace

void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, const AirportInfo &originAirport,
                       const AirportInfo &destAirport, int16_t screenWidth) {
  int16_t w = static_cast<int16_t>(screenWidth - 2 * kSideMargin);

  // No frame (dropped by request) — just text and pills on black.
  // Identity line — phase icon pinned at the panel's right edge,
  // airline/type cut so nothing overflows.
  const int16_t textX = static_cast<int16_t>(kSideMargin + kContentInset + 4);
  const int16_t textRightX = static_cast<int16_t>(kSideMargin + w - kContentInset - 4);
  drawIdentityLine(gfx, row, textX, kIdentityY, textRightX);

  // Route line, with country codes (Screen 1 detail level).
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.setTextDatum(top_left);
  const int16_t routeX = textX;
  gfx.drawString(
      fitToWidth(formatSummaryRoute(row.route, originAirport, destAirport, RouteFormat::WithCountry),
                 textRightX - routeX, gfxTextMeasure(gfx))
          .c_str(),
      routeX, kRouteY);

  // Altitude / speed / distance pills along the bottom of the frame.
  int16_t pillAreaX = static_cast<int16_t>(kSideMargin + kContentInset + 2);
  int16_t pillAreaW = static_cast<int16_t>(w - 2 * (kContentInset + 2));
  int16_t pillW = static_cast<int16_t>((pillAreaW - 2 * kPillGap) / 3);

  char buf[16];

  std::snprintf(buf, sizeof(buf), "%.0fm",
                row.aircraft.has_position ? static_cast<double>(row.aircraft.baro_altitude) : 0.0);
  drawPillButton(gfx, pillAreaX, kPillY, pillW, kPillHeight, THEME_PILL_FILL, THEME_CONTENT_SECONDARY, buf);

  std::snprintf(buf, sizeof(buf), "%.0fm/s",
                row.aircraft.has_position ? static_cast<double>(row.aircraft.velocity) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(pillAreaX + pillW + kPillGap), kPillY, pillW, kPillHeight,
                 THEME_PILL_FILL, THEME_CONTENT_SECONDARY, buf);

  std::snprintf(buf, sizeof(buf), "%.0fkm",
                row.has_distance ? static_cast<double>(row.distance_km) : 0.0);
  drawPillButton(gfx, static_cast<int16_t>(pillAreaX + 2 * (pillW + kPillGap)), kPillY, pillW,
                 kPillHeight, THEME_PILL_FILL, THEME_CONTENT_SECONDARY, buf);
}

void drawFeaturedPanelEmpty(LGFX &gfx, int16_t screenWidth) {
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.drawString(tr(Str::NoFlightsInRange), static_cast<int16_t>(screenWidth / 2),
                 static_cast<int16_t>(kPanelTop + kPanelHeight / 2));
}

#endif  // ARDUINO
