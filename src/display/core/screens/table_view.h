// table_view — draws Screen 1 (aircraft table), per docs/screens.md
// "Screen 1 — Flights". Composes featured_panel (the
// closest aircraft, spotlighted) with a paginated table of the rest. Uses
// lcars_theme for all chrome (panels/pills/fonts/colors) — defines none of
// its own. Zero networking code: it takes the AircraftRow list
// (airspace/core/tracked_flight, built by poll_pipeline) as plain data and
// only draws.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "airspace/core/feed_health.h"      // OpenSkyHealth
#include "airspace/core/tracked_flight.h"  // AircraftRow
#include "airspace/core/flight_ordering.h"  // splitFeaturedAndRest() — drawTablePage()
#include "display/core/screens/featured_panel.h"
#include "display/core/i18n.h"
#include "display/core/theme/lcars_theme.h"

// ---- Pure logic (no TFT/Arduino dependency) — testable under
// `pio test -e native`.

// Screen 1's table shows **exactly 5** data rows at a fixed step
// (tableFirstRowY() / tableRowHeightPx()), per docs/screens.md "Screen 1" —
// no height-derived row count. More than 5 aircraft in range are
// paged through via touch (getPageSlice()), not scrolled.
constexpr int kTableRowsPerPage = 5;

// Accessors so other modules (e.g. main.cpp's touch tap-zone sizing) stay
// in sync with these instead of hardcoding their own copies — see
// docs/review-notes-2026.md 5.5.
int tableRowsPerPage();   // kTableRowsPerPage (5)
int16_t tableRowHeightPx();  // the row step (kRowStep in table_view.cpp)
int16_t tableFirstRowY();    // 116 — y of the first data row

// One table column — the single source of truth shared by the lilac
// column-header row and the data rows, so header labels can never drift
// out of alignment with the data under them (docs/screens.md "Screen 1" column
// table). `x`/`w` are absolute px on the 320px-wide frame; columns are
// contiguous (each starts where the previous ends).
struct TableColumn {
  Str label;  // header label, translated via tr() (i18n)
  int16_t x;
  int16_t w;
};

constexpr int kTableColumnCount = 6;

// Inset of text from its column's left (and right) edge.
constexpr int16_t kTableCellPadPx = 2;

// Inset of a column-header label from its column's left edge (where the
// "|" divider line is drawn) — wider than the cell padding so the label
// doesn't sit right against the line.
constexpr int16_t kTableHeaderLabelPadPx = 4;

// Height of the lilac column-header strip; the row font is 1px smaller
// (LCARS_FONT_TABLE_HEIGHT_PX).
constexpr int16_t kTableHeaderHeightPx = 15;

// The 6 columns, in order: LOT/FLIGHT, ODL./DIST, SKĄD/FROM, DOKĄD/TO,
// TYP/TYPE, FAZA/PHASE.
const TableColumn *tableColumns();

// Pixel width available to a cell's text — what fitToWidth() cuts it to.
int tableCellTextWidthPx(const TableColumn &col);

// The ODL./DIST cell: distance from home, whole km ("8km"), or "--" when the
// aircraft has no position.
std::string tableDistanceText(const AircraftRow &row);

// Page indicator for the table header: "1/3" when there is more than one
// page (`page` is 0-based), "" otherwise. Drawn right-aligned in the TYP
// column header.
std::string tablePageLabel(int page, int pageCount);

// Number of pages needed to show `totalRows` records at `rowsPerPage` rows
// per page (ceiling division) — 0 records or a non-positive rowsPerPage
// both yield 0 pages, not 1. Pure — no TFT dependency.
int getPageCount(int totalRows, int rowsPerPage);

// The subset of `rows` to draw for `page` (0-based) at `rowsPerPage` rows
// per page. An out-of-range page (negative, or past the last page) returns
// an empty subset rather than out-of-bounds access — callers should check
// getPageCount() if they need to clamp `page` instead of just getting
// nothing back. Pure — no TFT dependency.
std::vector<AircraftRow> getPageSlice(const std::vector<AircraftRow> &rows, int page,
                                       int rowsPerPage);

// ---- Hardware adapter: actual drawing, via LovyanGFX + lcars_theme +
// featured_panel. Not covered by Unity (see CLAUDE.md "Testing"). Guarded
// here in the header too (like lcars_theme/featured_panel) since its
// signature needs the LGFX device type.
#ifdef ARDUINO

#include "display/adapters/lgfx/LGFX_CYD.hpp"

// Draws Screen 1's content area (LCARS_CONTENT_TOP..BOTTOM) across a
// `screenWidth`-px display, at the fixed coordinates docs/screens.md "Screen 1"
// specifies (kColHeaderY / kFirstRowY / kRowStep in table_view.cpp):
//   - featured_panel for the first aircraft in `rows` (or its "no aircraft
//     in range" placeholder if `rows` is empty);
//   - a lilac (THEME_TABLE_HEADER) column-header row, labels aligned to
//     the columns (tableColumns());
//   - exactly kTableRowsPerPage (5) data rows — flight, distance, origin
//     IATA, destination IATA, aircraft type, phase icon — for everyone
//     *except* the featured aircraft.
// `rows` must already be annotated (annotateDistances()), classified
// (classifyPhases()), and sorted (sortRowsByDistance()).
// `page` is
// 0-based over the *rest* of the list (the featured row is never repeated
// in the table); an out-of-range page draws no data rows. Which page is
// current, and reacting to touch, is main.cpp's job — this only lays out
// whichever page it's told to draw.
void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, int16_t screenWidth,
                   int page, OpenSkyHealth health);

#endif  // ARDUINO
