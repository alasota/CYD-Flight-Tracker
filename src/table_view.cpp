#include "table_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

void annotateDistances(std::vector<AircraftRow> &rows, float home_lat, float home_lon) {
  for (AircraftRow &row : rows) {
    if (!row.aircraft.has_position) {
      row.has_distance = false;
      row.distance_km = 0.0f;
      row.bearing_deg = 0.0f;
      continue;
    }
    DistanceBearing db =
        computeDistanceBearing(home_lat, home_lon, row.aircraft.lat, row.aircraft.lon);
    row.distance_km = db.distance_km;
    row.bearing_deg = db.bearing_deg;
    row.has_distance = true;
  }
}

void sortRowsByDistance(std::vector<AircraftRow> &rows) {
  std::stable_sort(rows.begin(), rows.end(), [](const AircraftRow &a, const AircraftRow &b) {
    float da = a.has_distance ? a.distance_km : std::numeric_limits<float>::infinity();
    float db = b.has_distance ? b.distance_km : std::numeric_limits<float>::infinity();
    return da < db;
  });
}

namespace {
constexpr int16_t kRowHeight = 20;
}  // namespace

int rowsPerPage(int16_t contentHeight) {
  if (contentHeight < kRowHeight) return 0;
  return contentHeight / kRowHeight;
}

#ifdef ARDUINO

#include <cstdio>

namespace {

std::string orDash(const std::string &s) { return s.empty() ? std::string("--") : s; }

}  // namespace

void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, int16_t x, int16_t y,
                    int16_t w, int16_t h, int page) {
  int perPage = rowsPerPage(h);
  if (perPage <= 0 || page < 0) return;

  size_t start = static_cast<size_t>(page) * static_cast<size_t>(perPage);
  if (start >= rows.size()) return;
  size_t end = std::min(rows.size(), start + static_cast<size_t>(perPage));

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_left);

  // Column x-offsets as fractions of the content width.
  int16_t colAirline = x;
  int16_t colFlight = x + static_cast<int16_t>(w * 0.34f);
  int16_t colType = x + static_cast<int16_t>(w * 0.50f);
  int16_t colAlt = x + static_cast<int16_t>(w * 0.68f);
  int16_t colSpeed = x + static_cast<int16_t>(w * 0.80f);
  int16_t colDist = x + static_cast<int16_t>(w * 0.90f);

  for (size_t i = start; i < end; ++i) {
    const AircraftRow &row = rows[i];
    int16_t rowY = y + static_cast<int16_t>(i - start) * kRowHeight;
    int16_t textY = rowY + kRowHeight / 2;

    // Rows must already be sorted ascending by distance (sortRowsByDistance())
    // — index 0 of the whole list is always the closest aircraft.
    bool isClosest = (i == 0);
    uint16_t bg = isClosest ? LCARS_AMBER : LCARS_BLACK;
    uint16_t fg = isClosest ? LCARS_BLACK : LCARS_PALE_BLUE;

    gfx.fillRect(x, rowY, w, kRowHeight, bg);
    gfx.setTextColor(fg, bg);

    gfx.drawString(orDash(row.info.airline).c_str(), colAirline + 2, textY);
    gfx.drawString(orDash(row.aircraft.callsign).c_str(), colFlight + 2, textY);
    gfx.drawString(orDash(row.info.aircraft_type).c_str(), colType + 2, textY);

    char buf[16];
    if (row.aircraft.has_position) {
      std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(row.aircraft.baro_altitude));
      gfx.drawString(buf, colAlt + 2, textY);
      std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(row.aircraft.velocity));
      gfx.drawString(buf, colSpeed + 2, textY);
    } else {
      gfx.drawString("--", colAlt + 2, textY);
      gfx.drawString("--", colSpeed + 2, textY);
    }

    if (row.has_distance) {
      std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(row.distance_km));
      gfx.drawString(buf, colDist + 2, textY);
    } else {
      gfx.drawString("--", colDist + 2, textY);
    }
  }
}

#endif  // ARDUINO
