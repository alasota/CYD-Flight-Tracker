#include "display/adapters/lgfx/table_view_draw.h"

#include <string>

#include "display/adapters/lgfx/featured_panel_draw.h"  // drawFeaturedPanel()
#include "display/adapters/lgfx/lcars_theme_draw.h"
#include "display/core/text/fit_to_width.h"

namespace {

// One cell: text cut to the column's width (fitToWidth) and drawn at the
// column's padded x, clipped to the column as a pixel-level backstop — so
// nothing ever spills into the neighbouring column.
void drawCell(LGFX &gfx, const TableColumn &col, const std::string &text, int16_t rowY,
              int16_t textY, const TextMeasure &measure) {
  gfx.setClipRect(col.x, rowY, col.w, kTableRowStep);
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
    int16_t rowY = static_cast<int16_t>(kTableFirstRowY + i * kTableRowStep);
    gfx.fillRect(0, rowY, screenWidth, kTableRowStep, LCARS_BLACK);

    if (i >= static_cast<int>(pageRows.size())) continue;
    const AircraftRow &row = pageRows[static_cast<size_t>(i)];
    int16_t textY = static_cast<int16_t>(rowY + kTableRowStep / 2);

    // White / sky alternating; aircraft on the ground dimmed (review #3).
    gfx.setTextColor(tableRowColor(i, row.aircraft.on_ground), LCARS_BLACK);
    // "..." = still loading, "--" = unknown; airports as IATA codes.
    drawCell(gfx, kTableColumns[kTableColFlight], summaryFlight(row), rowY, textY, measure);
    drawCell(gfx, kTableColumns[kTableColDistance], tableDistanceText(row), rowY, textY, measure);
    drawCell(gfx, kTableColumns[kTableColOrigin], rowAirportCode(row, /*origin=*/true), rowY, textY,
             measure);
    drawCell(gfx, kTableColumns[kTableColDest], rowAirportCode(row, /*origin=*/false), rowY, textY,
             measure);
    drawCell(gfx, kTableColumns[kTableColType], summaryType(row), rowY, textY, measure);
    // Phase: a small aircraft icon centred in its column — climbing /
    // descending / level side view, or a top view when on the ground;
    // nothing for "no phase".
    const TableColumn &phaseCol = kTableColumns[kTableColPhase];
    drawPhaseIcon(gfx, phaseIconKind(row.phase, row.aircraft.on_ground),
                  static_cast<int16_t>(phaseCol.x + phaseCol.w / 2), textY);
  }
}

// Lilac column-header row: each label at its own column's x (same
// kTableColumns as the data rows), with a 1px black "|" divider at every
// column boundary. LCARS_FONT_SMALL (efont 10px): the body font is taller
// than the strip allows with padding, and too wide for SKĄD/DOKĄD/FAZA.
void drawColumnHeader(LGFX &gfx, int16_t screenWidth, const std::string &pageLabel) {
  gfx.fillRect(0, kTableColHeaderY, screenWidth, kTableHeaderHeightPx, THEME_TABLE_HEADER);
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(THEME_ON_FILL, THEME_TABLE_HEADER);

  const int16_t textY = static_cast<int16_t>(kTableColHeaderY + kTableHeaderHeightPx / 2);
  for (int i = 0; i < kTableColumnCount; ++i) {
    const TableColumn &col = kTableColumns[i];
    if (i > 0) {
      gfx.drawFastVLine(col.x, static_cast<int16_t>(kTableColHeaderY + 2),
                        static_cast<int16_t>(kTableHeaderHeightPx - 4), LCARS_BLACK);
    }
    gfx.drawString(tr(col.label), static_cast<int16_t>(col.x + kTableHeaderLabelPadPx), textY);
  }

  // Page indicator ("1/3", review #4) right-aligned in the TYP column header
  // — only when there is more than one page.
  if (!pageLabel.empty()) {
    const TableColumn &typ = kTableColumns[kTableColType];
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

  // Lilac column-header row (kTableColHeaderY), labels aligned to the data
  // columns; efont carries the Polish letters, so "SKĄD"/"DOKĄD".
  const int pageCount = getPageCount(static_cast<int>(split.rest.size()), kTableRowsPerPage);
  drawColumnHeader(gfx, screenWidth, tablePageLabel(page, pageCount));

  // Exactly 5 data rows (kTableFirstRowY, kTableRowStep), over the list *without* the featured
  // aircraft; paging swaps which 5 (not scrolling).
  std::vector<AircraftRow> pageRows = getPageSlice(split.rest, page, kTableRowsPerPage);
  drawTableRows(gfx, pageRows, screenWidth);
}
