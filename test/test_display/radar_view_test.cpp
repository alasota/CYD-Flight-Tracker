// radar_view: Screen 3 layout + summary panel. docs/screens.md "Screen 3 — Radar".
#include <unity.h>

#include "radar_view.h"

namespace {

// ---- computeRadarLayout -----------------------------------------------------

static void test_layout_centers_in_content_area(void) {
  RadarLayout layout = computeRadarLayout(0, 0, 320, 240, 20);

  TEST_ASSERT_EQUAL_INT(160, layout.center_x);
  TEST_ASSERT_EQUAL_INT(120, layout.center_y);
  // Usable area after margin: 280x200 -> limited by height -> diameter 200 -> radius 100.
  TEST_ASSERT_EQUAL_INT(100, layout.radius_px);
}

static void test_layout_respects_content_area_offset(void) {
  RadarLayout layout = computeRadarLayout(10, 20, 320, 240, 20);

  // Center follows the offset, not just the width/height.
  TEST_ASSERT_EQUAL_INT(10 + 160, layout.center_x);
  TEST_ASSERT_EQUAL_INT(20 + 120, layout.center_y);
  TEST_ASSERT_EQUAL_INT(100, layout.radius_px);
}

static void test_layout_limited_by_smaller_dimension(void) {
  // A tall, narrow area — radius must be limited by width, not height.
  RadarLayout layout = computeRadarLayout(0, 0, 100, 300, 10);
  // usable: 80 x 280 -> diameter 80 -> radius 40.
  TEST_ASSERT_EQUAL_INT(40, layout.radius_px);
}

static void test_layout_zero_margin(void) {
  RadarLayout layout = computeRadarLayout(0, 0, 200, 200, 0);
  TEST_ASSERT_EQUAL_INT(100, layout.radius_px);
}

static void test_layout_margin_too_large_yields_zero_radius(void) {
  // Margin eats the whole area (or more) -> no room for any circle.
  RadarLayout layout = computeRadarLayout(0, 0, 40, 40, 30);
  TEST_ASSERT_EQUAL_INT(0, layout.radius_px);
}

static void test_screen3_radar_zone_resolves_to_spec_center_and_radius(void) {
  // docs/screens.md "Screen 3": radar zone x:0..190, y:18..215 (h 197), 10px
  // margin -> center (95,117), radius 85 — the exact values
  // radar_geometry::polarToScreen()/ring math is tested against.
  RadarLayout layout = computeRadarLayout(0, 18, 190, 197, 10);
  TEST_ASSERT_EQUAL_INT(95, layout.center_x);
  TEST_ASSERT_EQUAL_INT(117, layout.center_y);
  TEST_ASSERT_EQUAL_INT(85, layout.radius_px);
}

// ---- radiusDegToKm -----------------------------------------------------------

static void test_radius_deg_to_km_default_radius(void) {
  // config_store's default radius_deg is 2.5 -> ~278 km.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 278.0f, radiusDegToKm(2.5f));
}

static void test_radius_deg_to_km_one_degree(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 111.2f, radiusDegToKm(1.0f));
}

static void test_radius_deg_to_km_zero(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, radiusDegToKm(0.0f));
}

// ---- ring labels off the 12 o'clock spot (review #5) -------------------------

static void test_ring_label_sits_upper_right_on_the_ring(void) {
  // Labels used to sit at 12 o'clock, right where blips north of home land.
  RadarPoint p = ringLabelAnchor(95, 117, 85);
  TEST_ASSERT_TRUE(p.x > 95);   // right of centre
  TEST_ASSERT_TRUE(p.y < 117);  // above centre
  // ~45 degrees: equally far right and up.
  TEST_ASSERT_INT_WITHIN(1, p.x - 95, 117 - p.y);
  TEST_ASSERT_INT_WITHIN(1, 60, p.x - 95);  // 85 * cos(45) ~ 60
}

// ---- heading tick (review #7) ----------------------------------------------------

static void test_heading_tick_points_along_the_track(void) {
  RadarPoint n = headingTickEnd(50, 50, 0.0f, 9);
  TEST_ASSERT_EQUAL_INT(50, n.x);
  TEST_ASSERT_EQUAL_INT(41, n.y);  // north = up
  RadarPoint e = headingTickEnd(50, 50, 90.0f, 9);
  TEST_ASSERT_EQUAL_INT(59, e.x);
  TEST_ASSERT_EQUAL_INT(50, e.y);
  RadarPoint sw = headingTickEnd(50, 50, 225.0f, 10);
  TEST_ASSERT_TRUE(sw.x < 50 && sw.y > 50);
}

// ---- summary panel stats line (review #8) ------------------------------------------

static AircraftRow statsRow(bool onGround) {
  AircraftRow r;
  r.has_distance = true;
  r.distance_km = 4.2f;
  r.bearing_deg = 180.0f;
  r.aircraft.has_position = true;
  r.aircraft.on_ground = onGround;
  r.aircraft.has_altitude = true;
  r.aircraft.baro_altitude = 511.0f;
  return r;
}

static void test_summary_stats_airborne(void) {
  TEST_ASSERT_EQUAL_STRING("S 4km  511m", radarSummaryStats(statsRow(false), Language::PL).c_str());
}

static void test_summary_stats_on_ground(void) {
  TEST_ASSERT_EQUAL_STRING("S 4km  NA ZIEMI", radarSummaryStats(statsRow(true), Language::PL).c_str());
}

}  // namespace

void run_radar_view_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_layout_centers_in_content_area);
  RUN_TEST(test_layout_respects_content_area_offset);
  RUN_TEST(test_layout_limited_by_smaller_dimension);
  RUN_TEST(test_layout_zero_margin);
  RUN_TEST(test_layout_margin_too_large_yields_zero_radius);
  RUN_TEST(test_screen3_radar_zone_resolves_to_spec_center_and_radius);

  RUN_TEST(test_radius_deg_to_km_default_radius);
  RUN_TEST(test_radius_deg_to_km_one_degree);
  RUN_TEST(test_radius_deg_to_km_zero);

  RUN_TEST(test_ring_label_sits_upper_right_on_the_ring);
  RUN_TEST(test_heading_tick_points_along_the_track);
  RUN_TEST(test_summary_stats_airborne);
  RUN_TEST(test_summary_stats_on_ground);
}
