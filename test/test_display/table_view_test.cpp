// table_view: Screen 1 table: columns, rows, sorting, distance. docs/screens.md "Screen 1 — Flights".
#include <unity.h>

#include "fixtures.h"
#include "display/core/screens/table_view.h"
#include "display/core/text/utf8.h"  // utf8Length()
#include "test_support.h"

namespace {

// ---- splitFeaturedAndRest ---------------------------------------------------

static void test_split_featured_and_rest_multiple_rows(void) {
  AircraftRow closest, second, third;
  closest.aircraft.icao24 = "closest";
  second.aircraft.icao24 = "second";
  third.aircraft.icao24 = "third";

  std::vector<AircraftRow> rows = {closest, second, third};  // already sorted
  FeaturedSplit split = splitFeaturedAndRest(rows);

  TEST_ASSERT_TRUE(split.hasFeatured);
  TEST_ASSERT_EQUAL_STRING("closest", split.featured.aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_size_t(2, split.rest.size());
  TEST_ASSERT_EQUAL_STRING("second", split.rest[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("third", split.rest[1].aircraft.icao24.c_str());
}

static void test_split_featured_and_rest_single_row(void) {
  AircraftRow only;
  only.aircraft.icao24 = "only";

  std::vector<AircraftRow> rows = {only};
  FeaturedSplit split = splitFeaturedAndRest(rows);

  TEST_ASSERT_TRUE(split.hasFeatured);
  TEST_ASSERT_EQUAL_STRING("only", split.featured.aircraft.icao24.c_str());
  TEST_ASSERT_TRUE(split.rest.empty());
}

static void test_split_featured_and_rest_empty(void) {
  std::vector<AircraftRow> rows;
  FeaturedSplit split = splitFeaturedAndRest(rows);

  TEST_ASSERT_FALSE(split.hasFeatured);
  TEST_ASSERT_TRUE(split.rest.empty());
}

// ---- sortRowsByDistance -----------------------------------------------------

static void test_sort_orders_ascending_by_distance(void) {
  AircraftRow far, mid, near;
  far.aircraft.icao24 = "far";
  far.distance_km = 300.0f;
  far.has_distance = true;
  mid.aircraft.icao24 = "mid";
  mid.distance_km = 100.0f;
  mid.has_distance = true;
  near.aircraft.icao24 = "near";
  near.distance_km = 10.0f;
  near.has_distance = true;

  std::vector<AircraftRow> rows = {far, mid, near};
  sortRowsByDistance(rows);

  TEST_ASSERT_EQUAL_STRING("near", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("mid", rows[1].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("far", rows[2].aircraft.icao24.c_str());
}

static void test_sort_is_stable_for_equal_distances(void) {
  AircraftRow a, b, c;
  a.aircraft.icao24 = "a";
  a.distance_km = 50.0f;
  a.has_distance = true;
  b.aircraft.icao24 = "b";
  b.distance_km = 50.0f;
  b.has_distance = true;
  c.aircraft.icao24 = "c";
  c.distance_km = 50.0f;
  c.has_distance = true;

  std::vector<AircraftRow> rows = {a, b, c};
  sortRowsByDistance(rows);

  // All tied at 50km — original relative order (a, b, c) must be kept.
  TEST_ASSERT_EQUAL_STRING("a", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("b", rows[1].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("c", rows[2].aircraft.icao24.c_str());
}

static void test_sort_puts_rows_without_distance_last(void) {
  AircraftRow known, unknown;
  known.aircraft.icao24 = "known";
  known.distance_km = 42.0f;
  known.has_distance = true;
  unknown.aircraft.icao24 = "unknown";
  unknown.has_distance = false;

  std::vector<AircraftRow> rows = {unknown, known};
  sortRowsByDistance(rows);

  TEST_ASSERT_EQUAL_STRING("known", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("unknown", rows[1].aircraft.icao24.c_str());
}

static void test_sort_empty_array_does_not_crash(void) {
  std::vector<AircraftRow> rows;
  sortRowsByDistance(rows);
  TEST_ASSERT_TRUE(rows.empty());
}

// ---- sortRowsForDisplay: airborne first (improvement #9) -------------------

static void test_display_order_puts_airborne_before_ground_traffic(void) {
  // The plane taxiing at the airport 3 km away must not take the featured
  // slot from one flying 9 km away.
  std::vector<AircraftRow> rows = {makeRow("TAXI1", 3.0f, true), makeRow("AIR2", 9.0f, false),
                                   makeRow("AIR1", 5.0f, false), makeRow("TAXI2", 6.0f, true)};
  sortRowsForDisplay(rows);
  TEST_ASSERT_EQUAL_STRING("AIR1", rows[0].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("AIR2", rows[1].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("TAXI1", rows[2].aircraft.callsign.c_str());  // ground: nearest first
  TEST_ASSERT_EQUAL_STRING("TAXI2", rows[3].aircraft.callsign.c_str());
}

static void test_display_order_puts_positionless_last(void) {
  std::vector<AircraftRow> rows = {makeRow("NOPOS", 0.0f, false, false),
                                   makeRow("TAXI", 2.0f, true), makeRow("AIR", 20.0f, false)};
  sortRowsForDisplay(rows);
  TEST_ASSERT_EQUAL_STRING("AIR", rows[0].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("TAXI", rows[1].aircraft.callsign.c_str());
  TEST_ASSERT_EQUAL_STRING("NOPOS", rows[2].aircraft.callsign.c_str());
}

static void test_display_order_all_on_ground_still_nearest_first(void) {
  std::vector<AircraftRow> rows = {makeRow("B", 4.0f, true), makeRow("A", 1.0f, true)};
  sortRowsForDisplay(rows);
  TEST_ASSERT_EQUAL_STRING("A", rows[0].aircraft.callsign.c_str());
}

// ---- fixed table geometry (docs/screens.md "Screen 1") ----------------------------

static void test_table_shows_exactly_five_rows_per_page(void) {
  TEST_ASSERT_EQUAL_INT(5, kTableRowsPerPage);
  TEST_ASSERT_EQUAL_INT(5, tableRowsPerPage());
}

static void test_table_row_step_is_17px_from_y116(void) {
  // Rows at y = 116, 133, 150, 167, 184 — a 17px step from y:116 (docs/screens.md
  // "Screen 1": tight rows, 1px above the 16px Font2 text).
  TEST_ASSERT_EQUAL_INT(17, tableRowHeightPx());
  TEST_ASSERT_EQUAL_INT(116, tableFirstRowY());
  int16_t lastRowY = static_cast<int16_t>(tableFirstRowY() + (kTableRowsPerPage - 1) * tableRowHeightPx());
  TEST_ASSERT_EQUAL_INT(184, lastRowY);
  // The 5th row ends inside the 215px content boundary (25px bottom nav).
  TEST_ASSERT_TRUE(lastRowY + tableRowHeightPx() <= 215);
}

// ---- column layout (docs/screens.md "Screen 1" column table) ----------------------

static void test_phase_icon_fits_the_phase_column_and_row(void) {
  const TableColumn &phase = tableColumns()[5];
  TEST_ASSERT_TRUE(kPhaseIconW <= phase.w);
  TEST_ASSERT_TRUE(kPhaseIconH < tableRowHeightPx());  // 1px+ gap between rows' icons
}

static void test_columns_count_and_order(void) {
  TEST_ASSERT_EQUAL_INT(6, kTableColumnCount);
  const TableColumn *cols = tableColumns();
  TEST_ASSERT_EQUAL_STRING("LOT", tr(cols[0].label, Language::PL));
  TEST_ASSERT_EQUAL_STRING("ODL", tr(cols[1].label, Language::PL));  // no dot: it rendered like "+"
  TEST_ASSERT_EQUAL_STRING("SKĄ" "D", tr(cols[2].label, Language::PL));
  TEST_ASSERT_EQUAL_STRING("DOKĄ" "D", tr(cols[3].label, Language::PL));
  TEST_ASSERT_EQUAL_STRING("TYP", tr(cols[4].label, Language::PL));
  TEST_ASSERT_EQUAL_STRING("FAZA", tr(cols[5].label, Language::PL));

  TEST_ASSERT_EQUAL_STRING("FLIGHT", tr(cols[0].label, Language::EN));
  TEST_ASSERT_EQUAL_STRING("DIST", tr(cols[1].label, Language::EN));
  TEST_ASSERT_EQUAL_STRING("FROM", tr(cols[2].label, Language::EN));
  TEST_ASSERT_EQUAL_STRING("TO", tr(cols[3].label, Language::EN));
  TEST_ASSERT_EQUAL_STRING("TYPE", tr(cols[4].label, Language::EN));
  TEST_ASSERT_EQUAL_STRING("PHASE", tr(cols[5].label, Language::EN));
}

static void test_columns_are_contiguous_and_never_overlap(void) {
  const TableColumn *cols = tableColumns();
  for (int i = 0; i < kTableColumnCount; ++i) {
    TEST_ASSERT_TRUE(cols[i].w > 0);
    if (i > 0) {
      // Each column starts exactly where the previous one ends.
      TEST_ASSERT_EQUAL_INT(cols[i - 1].x + cols[i - 1].w, cols[i].x);
    }
  }
}

static void test_columns_fit_the_320px_screen(void) {
  const TableColumn *cols = tableColumns();
  TEST_ASSERT_TRUE(cols[0].x >= 0);
  const TableColumn &last = cols[kTableColumnCount - 1];
  TEST_ASSERT_TRUE(last.x + last.w <= 320);
}

static void test_column_labels_fit_their_columns_in_small_font(void) {
  // Labels are drawn in LCARS_FONT_SMALL (efont 10px, 5px per character) at
  // x + kTableHeaderLabelPadPx (clear of the divider line); they must not
  // reach the next column — in every
  // language.
  const TableColumn *cols = tableColumns();
  for (Language lang : {Language::PL, Language::EN}) {
    for (int i = 0; i < kTableColumnCount; ++i) {
      const char *label = tr(cols[i].label, lang);
      int labelPx = static_cast<int>(utf8Length(label)) * LCARS_FONT_SMALL_CHAR_PX;
      TEST_ASSERT_TRUE_MESSAGE(kTableHeaderLabelPadPx + labelPx <= cols[i].w, label);
    }
  }
}

static void test_row_font_is_one_px_smaller_than_the_header_strip(void) {
  // Rows use the 14px table font: 1px less than the 15px lilac header strip,
  // and it still fits the 17px row with room to spare.
  TEST_ASSERT_EQUAL_INT(kTableHeaderHeightPx - 1, LCARS_FONT_TABLE_HEIGHT_PX);
  TEST_ASSERT_TRUE(LCARS_FONT_TABLE_HEIGHT_PX < tableRowHeightPx());
}

static void test_header_labels_keep_clear_of_the_divider(void) {
  // Review note 9: labels sat right against the "|" divider line.
  TEST_ASSERT_TRUE(kTableHeaderLabelPadPx >= 4);
}

static void test_airport_columns_fit_an_iata_code(void) {
  // 3 letters of the table font (7px each).
  const TableColumn *cols = tableColumns();
  TEST_ASSERT_TRUE(tableCellTextWidthPx(cols[2]) >= 3 * LCARS_FONT_TABLE_CHAR_PX);
  TEST_ASSERT_TRUE(tableCellTextWidthPx(cols[3]) >= 3 * LCARS_FONT_TABLE_CHAR_PX);
}

static void test_callsign_and_distance_columns_never_truncate(void) {
  const TableColumn *cols = tableColumns();
  // An 8-character callsign ("WZZ1234A") and "123km".
  TEST_ASSERT_TRUE(tableCellTextWidthPx(cols[0]) >= 8 * LCARS_FONT_TABLE_CHAR_PX);
  TEST_ASSERT_TRUE(tableCellTextWidthPx(cols[1]) >= 5 * LCARS_FONT_TABLE_CHAR_PX);
}

static void test_type_column_fits_a_readable_type_name(void) {
  // "777-300ER" / "Dash 8-400" — the friendly names from aircraft_types.
  TEST_ASSERT_TRUE(tableCellTextWidthPx(tableColumns()[4]) >= 10 * LCARS_FONT_TABLE_CHAR_PX);
}

// ---- ground rows look different (review #3) ---------------------------------

static void test_ground_rows_are_dimmed(void) {
  // An aircraft on the ground listed after a 23 km airborne one looked like
  // a sorting bug; dimming it says "this one is parked".
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_MUTED, tableRowColor(0, /*onGround=*/true));
  TEST_ASSERT_EQUAL_HEX16(THEME_CONTENT_MUTED, tableRowColor(1, true));
  TEST_ASSERT_EQUAL_HEX16(tableRowTextColor(0), tableRowColor(0, false));
  TEST_ASSERT_EQUAL_HEX16(tableRowTextColor(1), tableRowColor(1, false));
}

// ---- page indicator (review #4) ---------------------------------------------------

static void test_page_label(void) {
  TEST_ASSERT_EQUAL_STRING("", tablePageLabel(0, 0).c_str());   // nothing to page
  TEST_ASSERT_EQUAL_STRING("", tablePageLabel(0, 1).c_str());   // single page: no clutter
  TEST_ASSERT_EQUAL_STRING("1/2", tablePageLabel(0, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("3/3", tablePageLabel(2, 3).c_str());
}

static void test_page_label_fits_the_type_column_header(void) {
  // Drawn right-aligned in the TYP column header, next to its "TYP"/"TYPE"
  // label, in the small font — "10/10" plus the label must fit.
  const TableColumn &typ = tableColumns()[4];
  const int labels = static_cast<int>(utf8Length("TYPE") + utf8Length("10/10")) *
                     LCARS_FONT_SMALL_CHAR_PX;
  TEST_ASSERT_TRUE(kTableHeaderLabelPadPx + labels + 8 <= typ.w);
}

static void test_distance_text(void) {
  AircraftRow row;
  TEST_ASSERT_EQUAL_STRING("--", tableDistanceText(row).c_str());  // no position
  row.has_distance = true;
  row.distance_km = 7.6f;
  TEST_ASSERT_EQUAL_STRING("8km", tableDistanceText(row).c_str());
  row.distance_km = 0.3f;
  TEST_ASSERT_EQUAL_STRING("0km", tableDistanceText(row).c_str());
}

static void test_cell_text_width_excludes_padding(void) {
  const TableColumn *cols = tableColumns();
  for (int i = 0; i < kTableColumnCount; ++i) {
    TEST_ASSERT_EQUAL_INT(cols[i].w - 2 * kTableCellPadPx, tableCellTextWidthPx(cols[i]));
  }
}

static void test_page_count_uses_fixed_five_row_pages(void) {
  TEST_ASSERT_EQUAL_INT(0, getPageCount(0, tableRowsPerPage()));
  TEST_ASSERT_EQUAL_INT(1, getPageCount(5, tableRowsPerPage()));
  TEST_ASSERT_EQUAL_INT(2, getPageCount(6, tableRowsPerPage()));
  TEST_ASSERT_EQUAL_INT(3, getPageCount(11, tableRowsPerPage()));
}

// ---- getPageCount -------------------------------------------------------------

static void test_page_count_zero_records(void) {
  TEST_ASSERT_EQUAL_INT(0, getPageCount(0, 10));
}

static void test_page_count_exact_multiple(void) {
  TEST_ASSERT_EQUAL_INT(3, getPageCount(30, 10));
}

static void test_page_count_inexact_multiple_rounds_up(void) {
  TEST_ASSERT_EQUAL_INT(3, getPageCount(25, 10));  // 2.5 -> 3 pages
  TEST_ASSERT_EQUAL_INT(1, getPageCount(1, 10));
}

static void test_page_count_non_positive_rows_per_page(void) {
  TEST_ASSERT_EQUAL_INT(0, getPageCount(25, 0));
  TEST_ASSERT_EQUAL_INT(0, getPageCount(25, -5));
}

// ---- getPageSlice -------------------------------------------------------------

static std::vector<AircraftRow> makeNumberedRows(int count) {
  std::vector<AircraftRow> rows;
  for (int i = 0; i < count; ++i) {
    AircraftRow row;
    row.aircraft.icao24 = std::to_string(i);
    rows.push_back(row);
  }
  return rows;
}

static void test_page_slice_first_page(void) {
  std::vector<AircraftRow> rows = makeNumberedRows(25);
  std::vector<AircraftRow> slice = getPageSlice(rows, 0, 10);

  TEST_ASSERT_EQUAL_size_t(10, slice.size());
  TEST_ASSERT_EQUAL_STRING("0", slice.front().aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("9", slice.back().aircraft.icao24.c_str());
}

static void test_page_slice_last_partial_page(void) {
  std::vector<AircraftRow> rows = makeNumberedRows(25);
  std::vector<AircraftRow> slice = getPageSlice(rows, 2, 10);  // page 2 = rows 20..24

  TEST_ASSERT_EQUAL_size_t(5, slice.size());
  TEST_ASSERT_EQUAL_STRING("20", slice.front().aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("24", slice.back().aircraft.icao24.c_str());
}

static void test_page_slice_out_of_range_page_is_empty(void) {
  std::vector<AircraftRow> rows = makeNumberedRows(25);

  TEST_ASSERT_TRUE(getPageSlice(rows, 3, 10).empty());   // past the last page (0,1,2)
  TEST_ASSERT_TRUE(getPageSlice(rows, -1, 10).empty());  // negative page
}

static void test_page_slice_empty_input(void) {
  std::vector<AircraftRow> rows;
  TEST_ASSERT_TRUE(getPageSlice(rows, 0, 10).empty());
}

}  // namespace

void run_table_view_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_phase_icon_fits_the_phase_column_and_row);




  RUN_TEST(test_sort_orders_ascending_by_distance);
  RUN_TEST(test_sort_is_stable_for_equal_distances);
  RUN_TEST(test_sort_puts_rows_without_distance_last);
  RUN_TEST(test_sort_empty_array_does_not_crash);
  RUN_TEST(test_display_order_puts_airborne_before_ground_traffic);
  RUN_TEST(test_display_order_puts_positionless_last);
  RUN_TEST(test_display_order_all_on_ground_still_nearest_first);

  RUN_TEST(test_split_featured_and_rest_multiple_rows);
  RUN_TEST(test_split_featured_and_rest_single_row);
  RUN_TEST(test_split_featured_and_rest_empty);

  RUN_TEST(test_table_shows_exactly_five_rows_per_page);
  RUN_TEST(test_table_row_step_is_17px_from_y116);
  RUN_TEST(test_page_count_uses_fixed_five_row_pages);

  RUN_TEST(test_columns_count_and_order);
  RUN_TEST(test_columns_are_contiguous_and_never_overlap);
  RUN_TEST(test_columns_fit_the_320px_screen);
  RUN_TEST(test_column_labels_fit_their_columns_in_small_font);
  RUN_TEST(test_cell_text_width_excludes_padding);
  RUN_TEST(test_callsign_and_distance_columns_never_truncate);
  RUN_TEST(test_type_column_fits_a_readable_type_name);
  RUN_TEST(test_distance_text);
  RUN_TEST(test_ground_rows_are_dimmed);
  RUN_TEST(test_page_label);
  RUN_TEST(test_page_label_fits_the_type_column_header);
  RUN_TEST(test_header_labels_keep_clear_of_the_divider);
  RUN_TEST(test_row_font_is_one_px_smaller_than_the_header_strip);
  RUN_TEST(test_airport_columns_fit_an_iata_code);

  RUN_TEST(test_page_count_zero_records);
  RUN_TEST(test_page_count_exact_multiple);
  RUN_TEST(test_page_count_inexact_multiple_rounds_up);
  RUN_TEST(test_page_count_non_positive_rows_per_page);

  RUN_TEST(test_page_slice_first_page);
  RUN_TEST(test_page_slice_last_partial_page);
  RUN_TEST(test_page_slice_out_of_range_page_is_empty);
  RUN_TEST(test_page_slice_empty_input);

}
