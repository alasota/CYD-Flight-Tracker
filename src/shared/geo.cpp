#include "shared/geo.h"

#include <cmath>

DistanceBearing computeDistanceBearing(float home_lat, float home_lon, float lat, float lon) {
  DistanceBearing result;

  if (home_lat == lat && home_lon == lon) {
    return result;  // distance=0, bearing=0 (undefined at zero distance)
  }

  constexpr float kEarthRadiusKm = 6371.0f;
  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

  float lat1 = home_lat * kDegToRad;
  float lat2 = lat * kDegToRad;
  float dLat = (lat - home_lat) * kDegToRad;
  float dLon = (lon - home_lon) * kDegToRad;

  float sinDLat2 = sinf(dLat * 0.5f);
  float sinDLon2 = sinf(dLon * 0.5f);
  float a = sinDLat2 * sinDLat2 + cosf(lat1) * cosf(lat2) * sinDLon2 * sinDLon2;
  float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
  result.distance_km = kEarthRadiusKm * c;

  float y = sinf(dLon) * cosf(lat2);
  float x = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(dLon);
  float bearingDeg = atan2f(y, x) * (180.0f / 3.14159265358979323846f);
  result.bearing_deg = fmodf(bearingDeg + 360.0f, 360.0f);

  return result;
}

const char *compassPoint(float bearingDeg) {
  static const char *const kPoints[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  float b = std::fmod(bearingDeg, 360.0f);
  if (b < 0.0f) b += 360.0f;
  const int sector = static_cast<int>((b + 22.5f) / 45.0f) % 8;
  return kPoints[sector];
}
