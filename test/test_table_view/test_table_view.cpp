#include <unity.h>

#include "table_view.h"

void setUp(void) {}
void tearDown(void) {}

// ---- computeDistanceBearing -----------------------------------------------

static void test_distance_zero_when_home_equals_aircraft(void) {
  DistanceBearing db = computeDistanceBearing(51.5074f, -0.1278f, 51.5074f, -0.1278f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, db.distance_km);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, db.bearing_deg);
}

static void test_distance_one_degree_north_at_equator(void) {
  // 1 deg of latitude is ~111.19 km anywhere on a sphere of radius 6371km.
  DistanceBearing db = computeDistanceBearing(0.0f, 0.0f, 1.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, db.distance_km);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, db.bearing_deg);  // due north
}

static void test_distance_one_degree_east_at_equator(void) {
  // At the equator, 1 deg of longitude is also ~111.19 km.
  DistanceBearing db = computeDistanceBearing(0.0f, 0.0f, 0.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, db.distance_km);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 90.0f, db.bearing_deg);  // due east
}

static void test_distance_london_to_paris_sanity_check(void) {
  // Well-known great-circle distance, commonly cited as ~344 km.
  DistanceBearing db = computeDistanceBearing(51.5074f, -0.1278f, 48.8566f, 2.3522f);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 344.0f, db.distance_km);
  TEST_ASSERT_TRUE(db.bearing_deg > 90.0f && db.bearing_deg < 180.0f);  // south-east
}

// ---- buildEnrichedRecords ---------------------------------------------------

static void test_build_enriched_records_found_true(void) {
  Aircraft ac;
  ac.icao24 = "4010ee";
  ac.callsign = "DLH9LH";
  ac.lat = 50.1210f;
  ac.lon = 6.1546f;
  ac.has_position = true;

  AircraftInfo info;
  info.found = true;
  info.airline = "easyJet Airline";
  info.aircraft_type = "A319 111";
  info.registration = "G-EZBZ";

  RouteInfo route;
  route.found = true;
  route.origin_icao = "EIDW";
  route.dest_icao = "EGLL";

  std::map<std::string, AircraftInfo> infoByIcao24 = {{"4010ee", info}};
  std::map<std::string, RouteInfo> routeByCallsign = {{"DLH9LH", route}};

  std::vector<AircraftRow> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_EQUAL_STRING("4010ee", rows[0].aircraft.icao24.c_str());
  TEST_ASSERT_EQUAL_STRING("DLH9LH", rows[0].aircraft.callsign.c_str());
  TEST_ASSERT_TRUE(rows[0].info.found);
  TEST_ASSERT_EQUAL_STRING("easyJet Airline", rows[0].info.airline.c_str());
  TEST_ASSERT_EQUAL_STRING("A319 111", rows[0].info.aircraft_type.c_str());
  TEST_ASSERT_TRUE(rows[0].route.found);
  TEST_ASSERT_EQUAL_STRING("EIDW", rows[0].route.origin_icao.c_str());
  TEST_ASSERT_EQUAL_STRING("EGLL", rows[0].route.dest_icao.c_str());
}

static void test_build_enriched_records_found_false_when_lookup_missing(void) {
  Aircraft ac;
  ac.icao24 = "a1b2c3";
  ac.callsign = "N12345";
  ac.has_position = true;

  // icao24/callsign never resolved (no entry at all — e.g. lookup still pending).
  std::map<std::string, AircraftInfo> infoByIcao24;
  std::map<std::string, RouteInfo> routeByCallsign;

  std::vector<AircraftRow> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_FALSE(rows[0].info.found);
  // drawTablePage()/featured_panel render this as "--" — see orDash()/
  // aircraft_summary::formatSummaryRoute().
  TEST_ASSERT_TRUE(rows[0].info.airline.empty());
  TEST_ASSERT_TRUE(rows[0].info.aircraft_type.empty());
}

static void test_build_enriched_records_missing_route_renders_as_dash(void) {
  // Airline/type lookup resolved, but route_lookup hasn't (yet) — the two
  // enrichment sources fail independently, per CLAUDE.md.
  Aircraft ac;
  ac.icao24 = "4010ee";
  ac.callsign = "DLH9LH";
  ac.has_position = true;

  AircraftInfo info;
  info.found = true;
  info.airline = "Lufthansa";
  info.aircraft_type = "A320";

  std::map<std::string, AircraftInfo> infoByIcao24 = {{"4010ee", info}};
  std::map<std::string, RouteInfo> routeByCallsign;  // empty — no route entry at all

  std::vector<AircraftRow> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_TRUE(rows[0].info.found);
  TEST_ASSERT_FALSE(rows[0].route.found);
  TEST_ASSERT_TRUE(rows[0].route.origin_icao.empty());
  TEST_ASSERT_TRUE(rows[0].route.dest_icao.empty());
}

static void test_build_enriched_records_found_false_from_cached_miss(void) {
  Aircraft ac;
  ac.icao24 = "ffffff";
  ac.has_position = true;

  // icao24 WAS looked up, and aircraft_lookup cached an explicit miss
  // (hexdb.io's "not found" response) — same result either way.
  AircraftInfo miss;  // found=false by default
  std::map<std::string, AircraftInfo> infoByIcao24 = {{"ffffff", miss}};
  std::map<std::string, RouteInfo> routeByCallsign;

  std::vector<AircraftRow> rows = buildEnrichedRecords({ac}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_EQUAL_size_t(1, rows.size());
  TEST_ASSERT_FALSE(rows[0].info.found);
}

static void test_build_enriched_records_empty_aircraft_list(void) {
  std::map<std::string, AircraftInfo> infoByIcao24 = {{"4010ee", AircraftInfo{}}};
  std::map<std::string, RouteInfo> routeByCallsign;

  std::vector<AircraftRow> rows = buildEnrichedRecords({}, infoByIcao24, routeByCallsign);

  TEST_ASSERT_TRUE(rows.empty());
}

// ---- classifyPhases ---------------------------------------------------------

static void test_classify_phases_fills_phase_from_distance_and_vertical_rate(void) {
  AircraftRow row;
  row.aircraft.vertical_rate = 5.0f;
  row.aircraft.on_ground = false;
  row.distance_km = 2.0f;
  row.has_distance = true;

  std::vector<AircraftRow> rows = {row};
  classifyPhases(rows, /*near_airport_km=*/5.0f, /*climb_threshold_mps=*/3.0f);

  TEST_ASSERT_TRUE(rows[0].phase == Phase::TAKEOFF);
}

static void test_classify_phases_no_distance_forces_none(void) {
  AircraftRow row;
  row.has_distance = false;  // no position -> nothing to classify
  row.aircraft.vertical_rate = 10.0f;

  std::vector<AircraftRow> rows = {row};
  classifyPhases(rows, 5.0f, 3.0f);

  TEST_ASSERT_TRUE(rows[0].phase == Phase::NONE);
}

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

// ---- annotateDistances -----------------------------------------------------

static void test_annotate_fills_distance_for_positioned_aircraft(void) {
  AircraftRow row;
  row.aircraft.icao24 = "a";
  row.aircraft.lat = 1.0f;
  row.aircraft.lon = 0.0f;
  row.aircraft.has_position = true;

  std::vector<AircraftRow> rows = {row};
  annotateDistances(rows, 0.0f, 0.0f);

  TEST_ASSERT_TRUE(rows[0].has_distance);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.19f, rows[0].distance_km);
}

static void test_annotate_leaves_no_position_aircraft_unset(void) {
  AircraftRow row;
  row.aircraft.icao24 = "noPos";
  row.aircraft.has_position = false;

  std::vector<AircraftRow> rows = {row};
  annotateDistances(rows, 0.0f, 0.0f);

  TEST_ASSERT_FALSE(rows[0].has_distance);
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

// ---- fixed table geometry (CLAUDE.md "Screen 1") ----------------------------

static void test_table_shows_exactly_five_rows_per_page(void) {
  TEST_ASSERT_EQUAL_INT(5, kTableRowsPerPage);
  TEST_ASSERT_EQUAL_INT(5, tableRowsPerPage());
}

static void test_table_row_step_is_18px_from_y140(void) {
  // Rows at y = 140, 158, 176, 194, 212 — an 18px step from y:140.
  TEST_ASSERT_EQUAL_INT(18, tableRowHeightPx());
  TEST_ASSERT_EQUAL_INT(140, tableFirstRowY());
  // The 5th row's top stays on the 320x240 frame.
  int16_t lastRowY = static_cast<int16_t>(tableFirstRowY() + (kTableRowsPerPage - 1) * tableRowHeightPx());
  TEST_ASSERT_EQUAL_INT(212, lastRowY);
  TEST_ASSERT_TRUE(lastRowY + tableRowHeightPx() <= 240);
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

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_distance_zero_when_home_equals_aircraft);
  RUN_TEST(test_distance_one_degree_north_at_equator);
  RUN_TEST(test_distance_one_degree_east_at_equator);
  RUN_TEST(test_distance_london_to_paris_sanity_check);

  RUN_TEST(test_build_enriched_records_found_true);
  RUN_TEST(test_build_enriched_records_found_false_when_lookup_missing);
  RUN_TEST(test_build_enriched_records_missing_route_renders_as_dash);
  RUN_TEST(test_build_enriched_records_found_false_from_cached_miss);
  RUN_TEST(test_build_enriched_records_empty_aircraft_list);

  RUN_TEST(test_annotate_fills_distance_for_positioned_aircraft);
  RUN_TEST(test_annotate_leaves_no_position_aircraft_unset);

  RUN_TEST(test_classify_phases_fills_phase_from_distance_and_vertical_rate);
  RUN_TEST(test_classify_phases_no_distance_forces_none);

  RUN_TEST(test_sort_orders_ascending_by_distance);
  RUN_TEST(test_sort_is_stable_for_equal_distances);
  RUN_TEST(test_sort_puts_rows_without_distance_last);
  RUN_TEST(test_sort_empty_array_does_not_crash);

  RUN_TEST(test_split_featured_and_rest_multiple_rows);
  RUN_TEST(test_split_featured_and_rest_single_row);
  RUN_TEST(test_split_featured_and_rest_empty);

  RUN_TEST(test_table_shows_exactly_five_rows_per_page);
  RUN_TEST(test_table_row_step_is_18px_from_y140);
  RUN_TEST(test_page_count_uses_fixed_five_row_pages);

  RUN_TEST(test_page_count_zero_records);
  RUN_TEST(test_page_count_exact_multiple);
  RUN_TEST(test_page_count_inexact_multiple_rounds_up);
  RUN_TEST(test_page_count_non_positive_rows_per_page);

  RUN_TEST(test_page_slice_first_page);
  RUN_TEST(test_page_slice_last_partial_page);
  RUN_TEST(test_page_slice_out_of_range_page_is_empty);
  RUN_TEST(test_page_slice_empty_input);

  return UNITY_END();
}
