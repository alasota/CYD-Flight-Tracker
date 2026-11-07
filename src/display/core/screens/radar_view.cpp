#include "display/core/screens/radar_view.h"

#include <algorithm>
#include <cmath>

#include "display/core/screens/featured_panel.h"  // featuredPillTexts()

namespace {
// Same km-per-degree approximation shared/geo's computeDistanceBearing()
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
  // Round the centre to nearest so an odd content height (y:18..215 -> 197)
  // lands on docs/screens.md's "Screen 3" (95, 117) rather than 1px low.
  layout.center_x = static_cast<int16_t>(x + (w + 1) / 2);
  layout.center_y = static_cast<int16_t>(y + (h + 1) / 2);
  return layout;
}

float radiusDegToKm(float radius_deg) { return radius_deg * kKmPerDegree; }

RadarPoint ringLabelAnchor(int16_t cx, int16_t cy, int16_t radiusPx) {
  constexpr float kCos45 = 0.70710678f;
  RadarPoint p;
  p.x = static_cast<int16_t>(std::lround(cx + radiusPx * kCos45));
  p.y = static_cast<int16_t>(std::lround(cy - radiusPx * kCos45));
  return p;
}

RadarPoint headingTickEnd(int16_t x, int16_t y, float trackDeg, int16_t len) {
  const float rad = trackDeg * 3.14159265f / 180.0f;
  RadarPoint p;
  p.x = static_cast<int16_t>(std::lround(x + len * std::sin(rad)));
  p.y = static_cast<int16_t>(std::lround(y - len * std::cos(rad)));  // screen y grows down
  return p;
}

std::string radarSummaryStats(const TrackedFlight &row, Language lang) {
  // Same values as the featured panel's chips: distance (with compass point)
  // first, then altitude — or "NA ZIEMI".
  const std::vector<std::string> p = featuredPillTexts(row, lang);
  if (p.size() == 2) return p[1] + "  " + p[0];  // "S 4km  NA ZIEMI"
  return p[2] + "  " + p[0];                     // "S 4km  511m"
}
