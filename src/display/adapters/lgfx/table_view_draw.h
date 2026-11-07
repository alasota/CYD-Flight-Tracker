// table_view_draw — the LovyanGFX half of table_view: Screen 1's content
// area (featured panel, column header, five rows of the current page).
// Hardware adapter, not covered by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>
#include <vector>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/table_view.h"

// Draws Screen 1's content area (LCARS_CONTENT_TOP..BOTTOM) across a
// `screenWidth`-px display, at the fixed coordinates docs/screens.md "Screen 1"
// specifies (kTableColHeaderY / kTableFirstRowY / kTableRowStep in table_view.h):
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
// current, and reacting to touch, is ui_flow's job (TablePager, routeTap()) — this only lays out
// whichever page it's told to draw.
void drawTablePage(LGFX &gfx, const std::vector<AircraftRow> &rows, int16_t screenWidth,
                   int page, FeedHealth health);
