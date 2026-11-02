#include "display/core/screens/table_view.h"

#include <algorithm>
#include <cstdio>

#include "display/core/text/fit_to_width.h"

// Columns: flight, distance, origin, destination, aircraft type, phase icon
// (see docs/screens.md "Screen 1" — altitude/speed/distance moved to
// featured_panel's pills, shown only for the one closest aircraft).
// Sized for the efont fonts (7px per character in the 14px rows, 5px in the
// header): LOT fits an 8-char callsign, ODL. "123km", SKĄD/DOKĄD a 3-letter
// IATA code, TYP a ~12-char friendly type name ("777-300ER"), FAZA the
// phase icon (16px pixel art) and its "PHASE" label. The airline column was dropped:
// the callsign already names the airline, and distance tells you where to
// look.
const TableColumn kTableColumns[kTableColumnCount] = {
    {Str::ColFlight, 2, 70}, {Str::ColDistance, 72, 46}, {Str::ColFrom, 118, 34},
    {Str::ColTo, 152, 34},   {Str::ColType, 186, 102},   {Str::ColPhase, 288, 30},
};

const TableColumn *tableColumns() { return kTableColumns; }

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
int16_t tableRowHeightPx() { return kTableRowStep; }
int16_t tableFirstRowY() { return kTableFirstRowY; }

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
