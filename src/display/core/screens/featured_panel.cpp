#include "display/core/screens/featured_panel.h"

#include <cstdio>

#include "shared/geo.h"                        // compassPoint()

int16_t featuredPanelTopPx() { return kFeaturedPanelTop; }
int16_t featuredPanelHeightPx() { return kFeaturedPanelHeight; }

namespace {
// OpenSky reports ground speed in m/s; km/h reads far better on a display.
constexpr float kMpsToKmh = 3.6f;

// "<n><unit>" rounded to a whole number, or "--" when the value is unknown.
std::string pillValue(bool known, float value, const char *unit) {
  if (!known) return kUnknownText;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%.0f%s", static_cast<double>(value), unit);
  return buf;
}
}  // namespace

std::vector<std::string> featuredPillTexts(const TrackedFlight &row, Language lang) {
  // Distance with its compass direction ("S 4km") — the arrow drawn next to
  // it shows the same thing graphically.
  const std::string distance =
      row.has_distance
          ? std::string(compassPoint(row.bearing_deg)) + " " + pillValue(true, row.distance_km, "km")
          : kUnknownText;
  if (row.aircraft.on_ground) {
    return {tr(Str::OnGround, lang), distance};
  }
  // "--" when OpenSky sent null — never a misleading "0m" / "0km/h".
  return {pillValue(row.aircraft.has_altitude, row.aircraft.baro_altitude, "m"),
          pillValue(row.aircraft.has_velocity, row.aircraft.velocity * kMpsToKmh, "km/h"),
          distance};
}
