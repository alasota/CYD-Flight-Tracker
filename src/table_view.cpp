#include "table_view.h"

#include <algorithm>
#include <cstdio>
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

namespace {
// Fixed layout from docs/screens.md "Screen 1", below the featured panel
// (y:18..95). The cyan LOTY/FLIGHTS sub-header strip was removed on request;
// the space went to roomier rows.
constexpr int16_t kColHeaderY = 98;    // lilac column-header bar, y:98..113
constexpr int16_t kColHeaderH = kTableHeaderHeightPx;
constexpr int16_t kFirstRowY = 116;    // rows at 116,133,150,167,184 (last ends y:201)
constexpr int16_t kRowStep = 17;       // LCARS_FONT_TABLE (14px) + gap; must end by 215
static_assert(kFirstRowY + kTableRowsPerPage * kRowStep <= LCARS_CONTENT_BOTTOM,
              "table rows must end above the bottom nav");

// Columns: flight, distance, origin, destination, aircraft type, phase icon
// (see docs/screens.md "Screen 1" — altitude/speed/distance moved to
// featured_panel's pills, shown only for the one closest aircraft).
// Sized for the efont fonts (7px per character in the 14px rows, 5px in the
// header): LOT fits an 8-char callsign, ODL. "123km", SKĄD/DOKĄD a 3-letter
// IATA code, TYP a ~12-char friendly type name ("777-300ER"), FAZA the
// phase icon (16px pixel art) and its "PHASE" label. The airline column was dropped:
// the callsign already names the airline, and distance tells you where to
// look.
constexpr TableColumn kColumns[kTableColumnCount] = {
    {Str::ColFlight, 2, 70}, {Str::ColDistance, 72, 46}, {Str::ColFrom, 118, 34},
    {Str::ColTo, 152, 34},   {Str::ColType, 186, 102},   {Str::ColPhase, 288, 30},
};
enum ColumnIndex { kColFlight, kColDistance, kColOrigin, kColDest, kColType, kColPhase };
}  // namespace

const TableColumn *tableColumns() { return kColumns; }

int tableCellTextWidthPx(const TableColumn &col) { return col.w - 2 * kTableCellPadPx; }

std::string tablePageLabel(int page, int pageCount) {
  if (pageCount <= 1) return std::string();
  return std::to_string(page + 1) + "/" + std::to_string(pageCount);
}

std::string tableDistanceText(const AircraftRow &row) {
  if (!row.has_distance) return kUnknownText;
  char buf[12];
  std::snprintf(buf, sizeof(buf), "%.0fkm", static_cast<double>(row.distance_km));
  return buf;
}

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
  gfx.setFont(LCARS_FONT_TABLE);  // 14px: 1px under the 15px header strip
  gfx.setTextDatum(middle_left);
  const TextMeasure measure = gfxTextMeasure(gfx);

  // Exactly kTableRowsPerPage slots; clear all of them so a short page
  // doesn't leave stale rows from a previous draw.
  for (int i = 0; i < kTableRowsPerPage; ++i) {
    int16_t rowY = static_cast<int16_t>(kFirstRowY + i * kRowStep);
    gfx.fillRect(0, rowY, screenWidth, kRowStep, LCARS_BLACK);

    if (i >= static_cast<int>(pageRows.size())) continue;
    const AircraftRow &row = pageRows[static_cast<size_t>(i)];
    int16_t textY = static_cast<int16_t>(rowY + kRowStep / 2);

    // White / sky alternating; aircraft on the ground dimmed (review #3).
    gfx.setTextColor(tableRowColor(i, row.aircraft.on_ground), LCARS_BLACK);
    // "..." = still loading, "--" = unknown; airports as IATA codes.
    drawCell(gfx, kColumns[kColFlight], summaryFlight(row), rowY, textY, measure);
    drawCell(gfx, kColumns[kColDistance], tableDistanceText(row), rowY, textY, measure);
    drawCell(gfx, kColumns[kColOrigin], rowAirportCode(row, /*origin=*/true), rowY, textY,
             measure);
    drawCell(gfx, kColumns[kColDest], rowAirportCode(row, /*origin=*/false), rowY, textY,
             measure);
    drawCell(gfx, kColumns[kColType], summaryType(row), rowY, textY, measure);
    // Phase: a small aircraft icon centred in its column — climbing /
    // descending / level side view, or a top view when on the ground;
    // nothing for "no phase".
    const TableColumn &phaseCol = kColumns[kColPhase];
    drawPhaseIcon(gfx, phaseIconKind(row.phase, row.aircraft.on_ground),
                  static_cast<int16_t>(phaseCol.x + phaseCol.w / 2), textY);
  }
}

// Lilac column-header row: each label at its own column's x (same
// kColumns as the data rows), with a 1px black "|" divider at every
// column boundary. LCARS_FONT_SMALL (efont 10px): the body font is taller
// than the strip allows with padding, and too wide for SKĄD/DOKĄD/FAZA.
void drawColumnHeader(LGFX &gfx, int16_t screenWidth, const std::string &pageLabel) {
  gfx.fillRect(0, kColHeaderY, screenWidth, kColHeaderH, THEME_TABLE_HEADER);
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(THEME_ON_FILL, THEME_TABLE_HEADER);

  const int16_t textY = static_cast<int16_t>(kColHeaderY + kColHeaderH / 2);
  for (int i = 0; i < kTableColumnCount; ++i) {
    const TableColumn &col = kColumns[i];
    if (i > 0) {
      gfx.drawFastVLine(col.x, static_cast<int16_t>(kColHeaderY + 2),
                        static_cast<int16_t>(kColHeaderH - 4), LCARS_BLACK);
    }
    gfx.drawString(tr(col.label), static_cast<int16_t>(col.x + kTableHeaderLabelPadPx), textY);
  }

  // Page indicator ("1/3", review #4) right-aligned in the TYP column header
  // — only when there is more than one page.
  if (!pageLabel.empty()) {
    const TableColumn &typ = kColumns[kColType];
    gfx.setTextDatum(middle_right);
    gfx.drawString(pageLabel.c_str(), static_cast<int16_t>(typ.x + typ.w - kTableHeaderLabelPadPx),
                   textY);
  }
}

}  // namespace

void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, int16_t screenWidth,
                   int page, OpenSkyHealth health) {
  FeaturedSplit split = splitFeaturedAndRest(rows);

  if (split.hasFeatured) {
    drawFeaturedPanel(gfx, split.featured, screenWidth);
  } else {
    drawFeaturedPanelEmpty(gfx, screenWidth, health);
  }

  // Lilac column-header row (kColHeaderY), labels aligned to the data
  // columns; efont carries the Polish letters, so "SKĄD"/"DOKĄD".
  const int pageCount = getPageCount(static_cast<int>(split.rest.size()), kTableRowsPerPage);
  drawColumnHeader(gfx, screenWidth, tablePageLabel(page, pageCount));

  // Exactly 5 data rows (kFirstRowY, kRowStep), over the list *without* the featured
  // aircraft; paging swaps which 5 (not scrolling).
  std::vector<AircraftRow> pageRows = getPageSlice(split.rest, page, kTableRowsPerPage);
  drawTableRows(gfx, pageRows, screenWidth);
}

#endif  // ARDUINO
