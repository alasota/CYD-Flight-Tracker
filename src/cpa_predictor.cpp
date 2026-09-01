#include "cpa_predictor.h"

#include <cmath>

namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kKmPerDegLat = 111.32;
// |v|^2 below this (km/s)^2 counts as "not moving": ~3e-5 km/s ≈ 0.03 m/s.
constexpr double kMinSpeedSquared = 1e-9;
}  // namespace

CpaPrediction predictCpa(double home_lat, double home_lon, double ac_lat, double ac_lon,
                         double speed_mps, double track_deg) {
  CpaPrediction result;

  const double lat_home_rad = home_lat * kDegToRad;
  const double dx_km = (ac_lon - home_lon) * kKmPerDegLat * std::cos(lat_home_rad);
  const double dy_km = (ac_lat - home_lat) * kKmPerDegLat;

  const double speed_kms = speed_mps / 1000.0;
  const double track_rad = track_deg * kDegToRad;
  const double vx = speed_kms * std::sin(track_rad);
  const double vy = speed_kms * std::cos(track_rad);

  const double v2 = vx * vx + vy * vy;
  if (v2 < kMinSpeedSquared) {
    return result;  // found stays false
  }

  result.found = true;
  result.t_cpa_seconds = static_cast<float>(-(dx_km * vx + dy_km * vy) / v2);
  return result;
}
