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

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_layout_centers_in_content_area);
  RUN_TEST(test_layout_respects_content_area_offset);
  RUN_TEST(test_layout_limited_by_smaller_dimension);
  RUN_TEST(test_layout_zero_margin);
  RUN_TEST(test_layout_margin_too_large_yields_zero_radius);

  RUN_TEST(test_radius_deg_to_km_default_radius);
  RUN_TEST(test_radius_deg_to_km_one_degree);
  RUN_TEST(test_radius_deg_to_km_zero);

  return UNITY_END();
}
