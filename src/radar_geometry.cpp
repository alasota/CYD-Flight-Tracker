#include "radar_geometry.h"

#include <cmath>

ScreenPoint polarToScreen(float bearing_deg, float distance_km, float max_distance_km,
                          int16_t center_x, int16_t center_y, int16_t radius_px) {
  ScreenPoint result;

  float ratio = 0.0f;
  if (max_distance_km > 0.0f) {
    ratio = distance_km / max_distance_km;
  }
  if (ratio < 0.0f) {
    ratio = 0.0f;  // a negative distance shouldn't happen, but don't invert the point if it does
  }
  if (ratio > 1.0f) {
    ratio = 1.0f;
    result.clamped = true;
  }

  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
  float bearingRad = bearing_deg * kDegToRad;
  float r = ratio * static_cast<float>(radius_px);

  // Compass convention (0=north/up, clockwise): x offset = sin(bearing),
  // y offset = -cos(bearing) since screen y grows downward.
  result.x = static_cast<int16_t>(center_x + lroundf(sinf(bearingRad) * r));
  result.y = static_cast<int16_t>(center_y - lroundf(cosf(bearingRad) * r));
  return result;
}

std::vector<float> computeRingDistances(float max_distance_km, int ring_count) {
  std::vector<float> result;
  if (ring_count <= 0) {
    return result;
  }

  result.reserve(static_cast<size_t>(ring_count));
  for (int i = 1; i <= ring_count; ++i) {
    result.push_back(max_distance_km * static_cast<float>(i) / static_cast<float>(ring_count));
  }
  return result;
}
