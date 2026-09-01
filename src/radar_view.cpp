#include "radar_view.h"

#include <algorithm>

namespace {
// Same km-per-degree approximation table_view's computeDistanceBearing()
// (haversine) is built on, for short distances.
constexpr float kKmPerDegree = 111.2f;
}  // namespace

RadarLayout computeRadarLayout(int16_t x, int16_t y, int16_t w, int16_t h, int16_t margin) {
  RadarLayout layout;

  int16_t usableW = static_cast<int16_t>(w - 2 * margin);
  int16_t usableH = static_cast<int16_t>(h - 2 * margin);
  int16_t diameter = std::min(usableW, usableH);
  if (diameter < 0) diameter = 0;

  layout.radius_px = static_cast<int16_t>(diameter / 2);
  layout.center_x = static_cast<int16_t>(x + w / 2);
  layout.center_y = static_cast<int16_t>(y + h / 2);
  return layout;
}

float radiusDegToKm(float radius_deg) { return radius_deg * kKmPerDegree; }

#ifdef ARDUINO

#include <cmath>
#include <cstdio>

#include "aircraft_summary.h"
#include "lcars_theme.h"

namespace {

// Fixed content-area layout, absolute coords on the 320x240 frame, below
// the 25px status_bar header — CLAUDE.md "Screen 3".
constexpr int16_t kContentTop = 28;
constexpr int16_t kContentHeight = 210;  // y:28..238

constexpr int16_t kRadarZoneX = 0;
constexpr int16_t kRadarZoneW = 190;
constexpr int16_t kRadarMargin = 10;  // -> center (95,133), radius 85

constexpr int16_t kDividerX = 192;
constexpr int16_t kDividerW = 8;  // x:192..200

constexpr int16_t kSummaryX = 200;
constexpr int16_t kSummaryW = 120;  // x:200..320
constexpr int16_t kSummaryCorner = 12;
constexpr int16_t kSummaryThickness = 3;
constexpr int16_t kSummaryTextX = 208;   // inside the frame, clear of the swept corner
constexpr int16_t kSummaryFlightY = 95;  // three-row baselines from CLAUDE.md
constexpr int16_t kSummaryAirlineY = 130;
constexpr int16_t kSummaryRouteY = 165;

constexpr int kRingCount = 3;
constexpr int16_t kBlipRadius = 5;
constexpr int16_t kClampedBlipRadius = 3;
// Dim teal for out-of-range (clamped) blips — deliberately not LCARS_CYAN
// so "off the scale" reads distinct from an in-range aircraft.
constexpr uint16_t kClampedBlipColor = 0x0410;
constexpr int16_t kHomeCrosshair = 5;

void drawRadar(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg) {
  RadarLayout layout =
      computeRadarLayout(kRadarZoneX, kContentTop, kRadarZoneW, kContentHeight, kRadarMargin);
  if (layout.radius_px <= 0) return;

  const float maxKm = radiusDegToKm(radius_deg);
  const std::vector<float> ringsKm = computeRingDistances(maxKm, kRingCount);

  // Concentric rings at r = radius_px * 1/3, 2/3, 3/3, each with its
  // km label (lcars_theme::drawRadarRing).
  for (int i = 0; i < kRingCount; ++i) {
    int16_t ringR = static_cast<int16_t>(layout.radius_px * (i + 1) / kRingCount);
    if (ringR <= 0) continue;
    int km = static_cast<int>(std::lround(ringsKm[static_cast<size_t>(i)]));
    drawRadarRing(gfx, layout.center_x, layout.center_y, ringR, km, LCARS_CYAN);
  }

  // Aircraft blips.
  for (const AircraftRow &row : rows) {
    if (!row.has_distance) continue;
    ScreenPoint p = polarToScreen(row.bearing_deg, row.distance_km, maxKm, layout.center_x,
                                  layout.center_y, layout.radius_px);
    if (p.clamped) {
      gfx.drawCircle(p.x, p.y, kClampedBlipRadius, kClampedBlipColor);
    } else {
      gfx.fillCircle(p.x, p.y, kBlipRadius, LCARS_CYAN);
    }
  }

  // Home crosshair (drawn last so nothing obscures it).
  gfx.drawFastHLine(static_cast<int16_t>(layout.center_x - kHomeCrosshair), layout.center_y,
                    static_cast<int16_t>(2 * kHomeCrosshair + 1), LCARS_YELLOW);
  gfx.drawFastVLine(layout.center_x, static_cast<int16_t>(layout.center_y - kHomeCrosshair),
                    static_cast<int16_t>(2 * kHomeCrosshair + 1), LCARS_YELLOW);
  gfx.fillCircle(layout.center_x, layout.center_y, 2, LCARS_YELLOW);
}

void drawSummaryPanel(LGFX &gfx, const std::vector<AircraftRow> &rows, const AirportInfo &origin,
                      const AirportInfo &dest) {
  drawElbowFrame(gfx, kSummaryX, kContentTop, kSummaryW, kContentHeight, kSummaryCorner,
                 kSummaryThickness, LCARS_MAGENTA);

  // Keep any long airline name from spilling past the frame into the radar.
  gfx.setClipRect(static_cast<int16_t>(kSummaryX + kSummaryThickness), kContentTop,
                  static_cast<int16_t>(kSummaryW - 2 * kSummaryThickness), kContentHeight);

  if (rows.empty()) {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
    gfx.drawString("BRAK LOTU", static_cast<int16_t>(kSummaryX + kSummaryW / 2),
                   static_cast<int16_t>(kContentTop + kContentHeight / 2));
    gfx.clearClipRect();
    return;
  }

  const AircraftRow &nearest = rows.front();

  gfx.setTextDatum(top_left);

  // Row 1: callsign, emphasized.
  gfx.setFont(LCARS_FONT_HEADING);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
  gfx.drawString(summaryFlight(nearest).c_str(), kSummaryTextX, kSummaryFlightY);

  // Row 2: airline (clipped to the panel by setClipRect above).
  gfx.setFont(LCARS_FONT_BODY);
  gfx.drawString(summaryAirline(nearest).c_str(), kSummaryTextX, kSummaryAirlineY);

  // Row 3: route, IATA codes only (no country codes — narrow panel).
  gfx.drawString(
      formatSummaryRoute(nearest.route, origin, dest, RouteFormat::CodesOnly).c_str(),
      kSummaryTextX, kSummaryRouteY);

  gfx.clearClipRect();
}

}  // namespace

void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg,
                   const AirportInfo &nearestOrigin, const AirportInfo &nearestDest,
                   int16_t screenWidth) {
  (void)screenWidth;  // fixed 320px layout — parameter kept for signature symmetry

  drawRadar(gfx, rows, radius_deg);

  drawVerticalDivider(gfx, kDividerX, kContentTop, kContentHeight, kDividerW, LCARS_ORANGE);

  drawSummaryPanel(gfx, rows, nearestOrigin, nearestDest);
}

#endif  // ARDUINO
