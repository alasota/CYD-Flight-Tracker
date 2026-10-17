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

// Columns: flight, airline, origin, destination, aircraft type, phase icon
// (see CLAUDE.md "Screen 1" — altitude/speed/distance moved to
// featured_panel's pills, shown only for the one closest aircraft).
// LOT is wide enough for an 8-char callsign in Font2; FAZA for its
// "FAZA" label in Font0.
constexpr TableColumn kColumns[kTableColumnCount] = {
    {"LOT", 2, 64},     {"LINIA", 66, 92}, {"SKAD", 158, 38},
    {"DOKAD", 196, 38}, {"TYP", 234, 58},  {"FAZA", 292, 26},
};
enum ColumnIndex { kColFlight, kColAirline, kColOrigin, kColDest, kColType, kColPhase };
}  // namespace

const TableColumn *tableColumns() { return kColumns; }

int tableCellTextWidthPx(const TableColumn &col) { return col.w - 2 * kTableCellPadPx; }

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

// One cell: text cut to the column's width (fitToWidth) and drawn at the
// column's padded x, clipped to the column as a pixel-level backstop — so
// nothing ever spills into the neighbouring column.
void drawCell(LGFX &gfx, const TableColumn &col, const std::string &text, int16_t rowY,
              int16_t textY, const TextMeasure &measure) {
  gfx.setClipRect(col.x, rowY, col.w, kRowStep);
  gfx.drawString(fitToWidth(text, tableCellTextWidthPx(col), measure).c_str(),
                 static_cast<int16_t>(col.x + kTableCellPadPx), textY);
  gfx.clearClipRect();
}

void drawTableRows(LGFX &gfx, const std::vector<AircraftRow> &pageRows, int16_t screenWidth) {
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
  const TextMeasure measure = gfxTextMeasure(gfx);

  // Exactly kTableRowsPerPage slots; clear all of them so a short page
  // doesn't leave stale rows from a previous draw.
  for (int i = 0; i < kTableRowsPerPage; ++i) {
    int16_t rowY = static_cast<int16_t>(kFirstRowY + i * kRowStep);
    gfx.fillRect(0, rowY, screenWidth, kRowStep, LCARS_BLACK);

    if (i >= static_cast<int>(pageRows.size())) continue;
    const AircraftRow &row = pageRows[static_cast<size_t>(i)];
    int16_t textY = static_cast<int16_t>(rowY + kRowStep / 2);

    drawCell(gfx, kColumns[kColFlight], orDash(row.aircraft.callsign), rowY, textY, measure);
    drawCell(gfx, kColumns[kColAirline], orDash(row.info.airline), rowY, textY, measure);
    drawCell(gfx, kColumns[kColOrigin], row.route.found ? row.route.origin_icao : "--", rowY,
             textY, measure);
    drawCell(gfx, kColumns[kColDest], row.route.found ? row.route.dest_icao : "--", rowY, textY,
             measure);
    drawCell(gfx, kColumns[kColType], orDash(row.info.aircraft_type), rowY, textY, measure);
    drawCell(gfx, kColumns[kColPhase], std::string(1, phaseIcon(row.phase)), rowY, textY,
             measure);
  }
}

// Orange column-header row: each label at its own column's x (same
// kColumns as the data rows), with a 1px black "|" divider at every
// column boundary. Font0 (8px) — Font2 is taller than the 15px strip
// allows with padding, and too wide for SKAD/DOKAD/FAZA.
void drawColumnHeader(LGFX &gfx, int16_t screenWidth) {
  gfx.fillRect(0, kColHeaderY, screenWidth, kColHeaderH, LCARS_ORANGE);
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(LCARS_BLACK, LCARS_ORANGE);

  const int16_t textY = static_cast<int16_t>(kColHeaderY + kColHeaderH / 2);
  for (int i = 0; i < kTableColumnCount; ++i) {
    const TableColumn &col = kColumns[i];
    if (i > 0) {
      gfx.drawFastVLine(col.x, static_cast<int16_t>(kColHeaderY + 2),
                        static_cast<int16_t>(kColHeaderH - 4), LCARS_BLACK);
    }
    gfx.drawString(col.label, static_cast<int16_t>(col.x + kTableCellPadPx), textY);
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
  // repeated label is intentional LCARS style, not a bug. Font0 (8px) so
  // the text and its background stay inside the 10px strip — Font2 (16px)
  // spilled cyan over the featured frame's bottom edge and the column
  // header.
  gfx.fillRect(0, kSubHeaderY, screenWidth, kSubHeaderH, LCARS_CYAN);
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(LCARS_BLACK, LCARS_CYAN);
  gfx.drawString("FLIGHTS", 4, static_cast<int16_t>(kSubHeaderY + kSubHeaderH / 2));

  // Orange column-header row at y:119..134, labels aligned to the data
  // columns (ASCII only: bitmap fonts don't carry the Polish diacritics —
  // so "SKAD"/"DOKAD", not "SKĄD"/"DOKĄD").
  drawColumnHeader(gfx, screenWidth);

  // Exactly 5 data rows from y:138 (17px step), over the list *without* the featured
  // aircraft; paging swaps which 5 (not scrolling).
  std::vector<AircraftRow> pageRows = getPageSlice(split.rest, page, kTableRowsPerPage);
  drawTableRows(gfx, pageRows, screenWidth);
}

#endif  // ARDUINO
