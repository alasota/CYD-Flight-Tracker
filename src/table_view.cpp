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

std::vector<AircraftRow> buildEnrichedRecords(
    const std::vector<Aircraft> &aircraft, const std::map<std::string, AircraftInfo> &infoByIcao24,
    const std::map<std::string, RouteInfo> &routeByCallsign) {
  std::vector<AircraftRow> rows;
  rows.reserve(aircraft.size());

  for (const Aircraft &ac : aircraft) {
    AircraftRow row;
    row.aircraft = ac;

    auto infoIt = infoByIcao24.find(ac.icao24);
    if (infoIt != infoByIcao24.end()) {
      row.info = infoIt->second;
    }
    // else: row.info stays default-constructed (found=false, empty
    // airline/aircraft_type) — rendered as "--".

    auto routeIt = routeByCallsign.find(ac.callsign);
    if (routeIt != routeByCallsign.end()) {
      row.route = routeIt->second;
    }
    // else: row.route stays default-constructed (found=false, empty
    // origin/dest) — rendered as "--".

    rows.push_back(row);
  }

  return rows;
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

void classifyPhases(std::vector<AircraftRow> &rows, float near_airport_km,
                     float climb_threshold_mps) {
  for (AircraftRow &row : rows) {
    if (!row.has_distance) {
      row.phase = Phase::NONE;  // no position -> nothing meaningful to classify
      continue;
    }
    row.phase = classifyPhase(row.distance_km, row.aircraft.vertical_rate, row.aircraft.on_ground,
                               near_airport_km, climb_threshold_mps);
  }
}

void sortRowsByDistance(std::vector<AircraftRow> &rows) {
  std::stable_sort(rows.begin(), rows.end(), [](const AircraftRow &a, const AircraftRow &b) {
    float da = a.has_distance ? a.distance_km : std::numeric_limits<float>::infinity();
    float db = b.has_distance ? b.distance_km : std::numeric_limits<float>::infinity();
    return da < db;
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

namespace {
// Fixed layout from CLAUDE.md "Screen 1" — absolute y on the 320x240
// frame, below the 25px status_bar header.
constexpr int16_t kSubHeaderY = 108;   // cyan "FLIGHTS" bar, y:108..118
constexpr int16_t kSubHeaderH = 10;    // thin label strip (CLAUDE.md "Screen 1")
constexpr int16_t kColHeaderY = 119;   // orange "|"-separated column labels, y:119..134
constexpr int16_t kColHeaderH = 15;    // text centered ~y:127
constexpr int16_t kFirstRowY = 138;    // rows at 138,155,172,189,206 (last text ends ~y:222)
constexpr int16_t kRowStep = 17;       // tightened from 18 for the y:225 content bound

// Column x-offsets, as fractions of the content width, for drawTableRows()
// below — named rather than left as bare literals inline (review notes
// 5.4). Columns: flight, airline, origin, destination, aircraft type,
// phase icon (see CLAUDE.md "Screen 1" — altitude/speed/distance moved to
// featured_panel's pills, shown only for the one closest aircraft).
constexpr float kColFlightFrac = 0.00f;
constexpr float kColAirlineFrac = 0.16f;
constexpr float kColOriginFrac = 0.50f;
constexpr float kColDestFrac = 0.62f;
constexpr float kColTypeFrac = 0.74f;
constexpr float kColPhaseFrac = 0.94f;
}  // namespace

int tableRowsPerPage() { return kTableRowsPerPage; }
int16_t tableRowHeightPx() { return kRowStep; }
int16_t tableFirstRowY() { return kFirstRowY; }

int getPageCount(int totalRows, int rowsPerPage) {
  if (totalRows <= 0 || rowsPerPage <= 0) return 0;
  return (totalRows + rowsPerPage - 1) / rowsPerPage;  // ceiling division
}

std::vector<AircraftRow> getPageSlice(const std::vector<AircraftRow> &rows, int page,
                                       int rowsPerPage) {
  std::vector<AircraftRow> slice;
  if (page < 0 || rowsPerPage <= 0) return slice;

  size_t start = static_cast<size_t>(page) * static_cast<size_t>(rowsPerPage);
  if (start >= rows.size()) return slice;

  size_t end = std::min(rows.size(), start + static_cast<size_t>(rowsPerPage));
  slice.assign(rows.begin() + static_cast<std::ptrdiff_t>(start),
               rows.begin() + static_cast<std::ptrdiff_t>(end));
  return slice;
}

#ifdef ARDUINO

namespace {

std::string orDash(const std::string &s) { return s.empty() ? std::string("--") : s; }

void drawTableRows(LGFX &gfx, const std::vector<AircraftRow> &pageRows, int16_t screenWidth) {
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);

  int16_t w = screenWidth;
  int16_t colFlight = static_cast<int16_t>(w * kColFlightFrac);
  int16_t colAirline = static_cast<int16_t>(w * kColAirlineFrac);
  int16_t colOrigin = static_cast<int16_t>(w * kColOriginFrac);
  int16_t colDest = static_cast<int16_t>(w * kColDestFrac);
  int16_t colType = static_cast<int16_t>(w * kColTypeFrac);
  int16_t colPhase = static_cast<int16_t>(w * kColPhaseFrac);

  // Exactly kTableRowsPerPage slots; clear all of them so a short page
  // doesn't leave stale rows from a previous draw.
  for (int i = 0; i < kTableRowsPerPage; ++i) {
    int16_t rowY = static_cast<int16_t>(kFirstRowY + i * kRowStep);
    gfx.fillRect(0, rowY, w, kRowStep, LCARS_BLACK);

    if (i >= static_cast<int>(pageRows.size())) continue;
    const AircraftRow &row = pageRows[static_cast<size_t>(i)];
    int16_t textY = static_cast<int16_t>(rowY + kRowStep / 2);

    gfx.drawString(orDash(row.aircraft.callsign).c_str(), colFlight + 2, textY);
    gfx.drawString(orDash(row.info.airline).c_str(), colAirline + 2, textY);
    gfx.drawString(row.route.found ? row.route.origin_icao.c_str() : "--", colOrigin + 2, textY);
    gfx.drawString(row.route.found ? row.route.dest_icao.c_str() : "--", colDest + 2, textY);
    gfx.drawString(orDash(row.info.aircraft_type).c_str(), colType + 2, textY);

    char phaseBuf[2] = {phaseIcon(row.phase), '\0'};
    gfx.drawString(phaseBuf, colPhase + 2, textY);
  }
}

}  // namespace

void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, const AirportInfo &originAirport,
                    const AirportInfo &destAirport, int16_t screenWidth, int page) {
  FeaturedSplit split = splitFeaturedAndRest(rows);

  if (split.hasFeatured) {
    drawFeaturedPanel(gfx, split.featured, originAirport, destAirport, screenWidth);
  } else {
    drawFeaturedPanelEmpty(gfx, screenWidth);
  }

  // Cyan "FLIGHTS" sub-header bar (black text) — CLAUDE.md notes the
  // repeated label is intentional LCARS style, not a bug.
  gfx.fillRect(0, kSubHeaderY, screenWidth, kSubHeaderH, LCARS_CYAN);
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(LCARS_BLACK, LCARS_CYAN);
  gfx.drawString("FLIGHTS", 4, static_cast<int16_t>(kSubHeaderY + kSubHeaderH / 2));

  // Orange column-header row at y:119..134 (text centred ~y:127) — one "|"-separated string, black
  // text (ASCII only: bitmap fonts don't carry the Polish diacritics, per
  // CLAUDE.md table_view note — so "SKAD"/"DOKAD", not "SKĄD"/"DOKĄD").
  gfx.fillRect(0, kColHeaderY, screenWidth, kColHeaderH, LCARS_ORANGE);
  gfx.setTextColor(LCARS_BLACK, LCARS_ORANGE);
  gfx.drawString("LOT | LINIA | SKAD | DOKAD | TYP | FAZA", 4,
                 static_cast<int16_t>(kColHeaderY + kColHeaderH / 2));

  // Exactly 5 data rows from y:138 (17px step), over the list *without* the featured
  // aircraft; paging swaps which 5 (not scrolling).
  std::vector<AircraftRow> pageRows = getPageSlice(split.rest, page, kTableRowsPerPage);
  drawTableRows(gfx, pageRows, screenWidth);
}

#endif  // ARDUINO
