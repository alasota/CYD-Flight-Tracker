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

#include <cstdio>

namespace {

constexpr int kRingCount = 4;
constexpr int16_t kMargin = 24;  // leaves room for ring-distance labels near the plot's edge
constexpr int16_t kHomeMarkerRadius = 5;
constexpr int16_t kHomeRingRadius = 9;
constexpr int16_t kBlipRadius = 4;
constexpr int16_t kClampedBlipRadius = 3;

}  // namespace

void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg, int16_t x,
                    int16_t y, int16_t w, int16_t h) {
  gfx.fillRect(x, y, w, h, LCARS_BLACK);

  RadarLayout layout = computeRadarLayout(x, y, w, h, kMargin);
  if (layout.radius_px <= 0) return;  // content area too small to plot anything

  float maxKm = radiusDegToKm(radius_deg);

  // Concentric distance rings, with labels — same lcars_theme accent
  // table_view uses for its own row text (LCARS_PALE_BLUE), same single
  // consistent stroke for every ring.
  std::vector<float> ringsKm = computeRingDistances(maxKm, kRingCount);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(top_left);
  gfx.setTextColor(LCARS_PALE_BLUE, LCARS_BLACK);

  for (float ringKm : ringsKm) {
    int16_t ringRadiusPx =
        (maxKm > 0.0f) ? static_cast<int16_t>(layout.radius_px * (ringKm / maxKm)) : 0;
    if (ringRadiusPx <= 0) continue;

    gfx.drawCircle(layout.center_x, layout.center_y, ringRadiusPx, LCARS_PALE_BLUE);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0fkm", static_cast<double>(ringKm));
    gfx.drawString(buf, static_cast<int16_t>(layout.center_x + 3),
                   static_cast<int16_t>(layout.center_y - ringRadiusPx));
  }

  // Aircraft blips — normal ones filled/full-size in LCARS_ROSE; ones
  // clamped to the outer ring (out of configured range) drawn dimmed:
  // smaller, outline-only, and in the paler LCARS_PALE_BLUE rather than
  // the vivid blip color, so they read as visibly distinct from an
  // in-range aircraft rather than just "at the edge".
  for (const AircraftRow &row : rows) {
    if (!row.has_distance) continue;  // no position -> nothing to plot

    ScreenPoint p = polarToScreen(row.bearing_deg, row.distance_km, maxKm, layout.center_x,
                                  layout.center_y, layout.radius_px);
    if (p.clamped) {
      gfx.drawCircle(p.x, p.y, kClampedBlipRadius, LCARS_PALE_BLUE);
    } else {
      gfx.fillCircle(p.x, p.y, kBlipRadius, LCARS_ROSE);
    }
  }

  // Home marker, drawn last so it's never obscured by a ring or blip.
  gfx.drawCircle(layout.center_x, layout.center_y, kHomeRingRadius, LCARS_AMBER);
  gfx.fillCircle(layout.center_x, layout.center_y, kHomeMarkerRadius, LCARS_AMBER);
}

#endif  // ARDUINO
