#include "airspace/core/flight_ordering.h"

#include <algorithm>
#include <limits>

void sortRowsByDistance(std::vector<AircraftRow> &rows) {
  std::stable_sort(rows.begin(), rows.end(), [](const AircraftRow &a, const AircraftRow &b) {
    float da = a.has_distance ? a.distance_km : std::numeric_limits<float>::infinity();
    float db = b.has_distance ? b.distance_km : std::numeric_limits<float>::infinity();
    return da < db;
  });
}

void sortRowsForDisplay(std::vector<AircraftRow> &rows) {
  // Group: 0 = airborne with position, 1 = on the ground, 2 = no position.
  auto group = [](const AircraftRow &r) {
    if (!r.has_distance) return 2;
    return r.aircraft.on_ground ? 1 : 0;
  };
  std::stable_sort(rows.begin(), rows.end(), [&](const AircraftRow &a, const AircraftRow &b) {
    const int ga = group(a);
    const int gb = group(b);
    if (ga != gb) return ga < gb;
    return a.distance_km < b.distance_km;  // equal for group 2 -> stable
  });
}

FeaturedSplit splitFeaturedAndRest(const std::vector<AircraftRow> &rows) {
  FeaturedSplit split;
  if (rows.empty()) {
    return split;
  }

  split.hasFeatured = true;
  split.featured = rows.front();
  split.rest.assign(rows.begin() + 1, rows.end());
  return split;
}
