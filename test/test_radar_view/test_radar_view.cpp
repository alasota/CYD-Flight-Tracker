#include <unity.h>

#include "radar_view.h"

void setUp(void) {}
void tearDown(void) {}

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
  // CLAUDE.md "Screen 3": radar zone x:0..190, y:28..225 (h 197), 10px
  // margin -> center (95,127), radius 85 — the exact values
  // radar_geometry::polarToScreen()/ring math is tested against.
  RadarLayout layout = computeRadarLayout(0, 28, 190, 197, 10);
  TEST_ASSERT_EQUAL_INT(95, layout.center_x);
  TEST_ASSERT_EQUAL_INT(127, layout.center_y);
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


// ---- truncateAirline ------------------------------------------------------

static void test_truncate_airline_short_string_unchanged(void) {
  TEST_ASSERT_EQUAL_STRING("Ryanair", truncateAirline("Ryanair", 15).c_str());
}

static void test_truncate_airline_exact_length_unchanged(void) {
  // size == maxChars is a fit, not an overflow.
  TEST_ASSERT_EQUAL_STRING("123456789012345", truncateAirline("123456789012345", 15).c_str());
}

static void test_truncate_airline_long_string_gets_ellipsis(void) {
  std::string out = truncateAirline("LOT Polish Airlines", 13);
  TEST_ASSERT_EQUAL_STRING("LOT Polish...", out.c_str());
  TEST_ASSERT_TRUE(out.size() <= 13);
}

static void test_truncate_airline_trims_trailing_space_before_ellipsis(void) {
  // Head would end "...LOT " — the space is dropped so it's not "LOT ...".
  TEST_ASSERT_EQUAL_STRING("LOT...", truncateAirline("LOT Airlines", 7).c_str());
}

static void test_truncate_airline_result_never_exceeds_maxchars(void) {
  const std::string name = "Scandinavian Airlines System Denmark Norway Sweden";
  for (int m = 1; m <= 20; ++m) {
    TEST_ASSERT_TRUE(static_cast<int>(truncateAirline(name, m).size()) <= m);
  }
}

static void test_truncate_airline_too_tight_for_ellipsis_hard_cuts(void) {
  // maxChars < 4 can't fit "X..." — plain cut, no ellipsis.
  TEST_ASSERT_EQUAL_STRING("Lu", truncateAirline("Lufthansa", 2).c_str());
  TEST_ASSERT_EQUAL_STRING("Luf", truncateAirline("Lufthansa", 3).c_str());
}

static void test_truncate_airline_nonpositive_maxchars_is_empty(void) {
  TEST_ASSERT_EQUAL_STRING("", truncateAirline("Lufthansa", 0).c_str());
  TEST_ASSERT_EQUAL_STRING("", truncateAirline("Lufthansa", -5).c_str());
}

static void test_truncate_airline_ascii_ellipsis_only(void) {
  // Must stay within the bitmap font's 0x20-0x7E range (no "…").
  std::string out = truncateAirline("Some Very Long Airline Name Ltd", 12);
  for (char c : out) {
    TEST_ASSERT_TRUE(static_cast<unsigned char>(c) >= 0x20 &&
                     static_cast<unsigned char>(c) <= 0x7E);
  }
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_layout_centers_in_content_area);
  RUN_TEST(test_layout_respects_content_area_offset);
  RUN_TEST(test_layout_limited_by_smaller_dimension);
  RUN_TEST(test_layout_zero_margin);
  RUN_TEST(test_layout_margin_too_large_yields_zero_radius);
  RUN_TEST(test_screen3_radar_zone_resolves_to_spec_center_and_radius);

  RUN_TEST(test_radius_deg_to_km_default_radius);
  RUN_TEST(test_radius_deg_to_km_one_degree);
  RUN_TEST(test_radius_deg_to_km_zero);

  RUN_TEST(test_truncate_airline_short_string_unchanged);
  RUN_TEST(test_truncate_airline_exact_length_unchanged);
  RUN_TEST(test_truncate_airline_long_string_gets_ellipsis);
  RUN_TEST(test_truncate_airline_trims_trailing_space_before_ellipsis);
  RUN_TEST(test_truncate_airline_result_never_exceeds_maxchars);
  RUN_TEST(test_truncate_airline_too_tight_for_ellipsis_hard_cuts);
  RUN_TEST(test_truncate_airline_nonpositive_maxchars_is_empty);
  RUN_TEST(test_truncate_airline_ascii_ellipsis_only);

  return UNITY_END();
}
