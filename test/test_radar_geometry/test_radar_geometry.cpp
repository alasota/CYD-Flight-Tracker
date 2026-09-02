#include <unity.h>

#include <cmath>

#include "radar_geometry.h"

void setUp(void) {}
void tearDown(void) {}

// Shared plot geometry for the tests below: center (160,120), radius
// 100px, scan radius 50km.
static constexpr int16_t kCx = 160;
static constexpr int16_t kCy = 120;
static constexpr int16_t kRadiusPx = 100;
static constexpr float kMaxKm = 50.0f;

// ---- polarToScreen: known cardinal angles ----------------------------------

static void test_bearing_0_deg_is_straight_up(void) {
  // North/0deg -> up: x unchanged, y decreases (screen y grows downward).
  ScreenPoint p = polarToScreen(0.0f, 25.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(160, p.x);
  TEST_ASSERT_EQUAL_INT(70, p.y);  // 120 - 50 (half of radius_px, since 25/50 = 0.5)
  TEST_ASSERT_FALSE(p.clamped);
}

static void test_bearing_90_deg_is_straight_right(void) {
  // East/90deg -> right: x increases, y unchanged.
  ScreenPoint p = polarToScreen(90.0f, 25.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(210, p.x);
  TEST_ASSERT_EQUAL_INT(120, p.y);
  TEST_ASSERT_FALSE(p.clamped);
}

static void test_bearing_180_deg_is_straight_down(void) {
  // South/180deg -> down: x unchanged, y increases.
  ScreenPoint p = polarToScreen(180.0f, 25.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(160, p.x);
  TEST_ASSERT_EQUAL_INT(170, p.y);
  TEST_ASSERT_FALSE(p.clamped);
}

static void test_bearing_270_deg_is_straight_left(void) {
  // West/270deg -> left: x decreases, y unchanged.
  ScreenPoint p = polarToScreen(270.0f, 25.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(110, p.x);
  TEST_ASSERT_EQUAL_INT(120, p.y);
  TEST_ASSERT_FALSE(p.clamped);
}

// ---- polarToScreen: several distances at a fixed bearing -------------------

static void test_several_distances_scale_linearly_along_north(void) {
  ScreenPoint quarter = polarToScreen(0.0f, 12.5f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(kCy - 25, quarter.y);  // 12.5/50 = 0.25 -> 25px

  ScreenPoint half = polarToScreen(0.0f, 25.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(kCy - 50, half.y);  // 0.5 -> 50px

  ScreenPoint full = polarToScreen(0.0f, 50.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(kCy - 100, full.y);  // 1.0 -> 100px, exactly at the ring edge
  TEST_ASSERT_FALSE(full.clamped);           // == max, not beyond it
}

// ---- polarToScreen: distance_km == 0 (home = center) ------------------------

static void test_zero_distance_is_center_regardless_of_bearing(void) {
  ScreenPoint north = polarToScreen(0.0f, 0.0f, kMaxKm, kCx, kCy, kRadiusPx);
  ScreenPoint east = polarToScreen(90.0f, 0.0f, kMaxKm, kCx, kCy, kRadiusPx);
  ScreenPoint south = polarToScreen(180.0f, 0.0f, kMaxKm, kCx, kCy, kRadiusPx);

  TEST_ASSERT_EQUAL_INT(kCx, north.x);
  TEST_ASSERT_EQUAL_INT(kCy, north.y);
  TEST_ASSERT_EQUAL_INT(kCx, east.x);
  TEST_ASSERT_EQUAL_INT(kCy, east.y);
  TEST_ASSERT_EQUAL_INT(kCx, south.x);
  TEST_ASSERT_EQUAL_INT(kCy, south.y);

  TEST_ASSERT_FALSE(north.clamped);
}

// ---- polarToScreen: beyond max_distance_km (clamped) -----------------------

static void test_beyond_max_distance_clamps_to_ring_edge(void) {
  ScreenPoint p = polarToScreen(90.0f, 500.0f, kMaxKm, kCx, kCy, kRadiusPx);  // 10x over range

  TEST_ASSERT_TRUE(p.clamped);
  // Exactly radius_px from center — same position as distance_km==max_km
  // at this bearing (east).
  TEST_ASSERT_EQUAL_INT(kCx + kRadiusPx, p.x);
  TEST_ASSERT_EQUAL_INT(kCy, p.y);
}

static void test_beyond_max_distance_lands_exactly_on_circle_at_any_bearing(void) {
  // A non-axis-aligned bearing, to confirm the clamped point is genuinely
  // radius_px away from center (not just correct on the two axes).
  ScreenPoint p = polarToScreen(37.0f, 999.0f, kMaxKm, kCx, kCy, kRadiusPx);

  TEST_ASSERT_TRUE(p.clamped);
  float dx = static_cast<float>(p.x - kCx);
  float dy = static_cast<float>(p.y - kCy);
  float dist = sqrtf(dx * dx + dy * dy);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, static_cast<float>(kRadiusPx), dist);  // rounding tolerance
}

static void test_negative_distance_does_not_invert_point(void) {
  // Shouldn't happen in practice, but a negative input must not flip the
  // point to the opposite side of the plot.
  ScreenPoint p = polarToScreen(0.0f, -10.0f, kMaxKm, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(kCx, p.x);
  TEST_ASSERT_EQUAL_INT(kCy, p.y);
}

static void test_non_positive_max_distance_collapses_to_center(void) {
  // Defensive: config_store's radius clamp keeps this from happening in
  // practice, but a zero/negative max shouldn't divide by zero or produce
  // garbage coordinates.
  ScreenPoint p = polarToScreen(45.0f, 10.0f, 0.0f, kCx, kCy, kRadiusPx);
  TEST_ASSERT_EQUAL_INT(kCx, p.x);
  TEST_ASSERT_EQUAL_INT(kCy, p.y);
}

// ---- computeRingDistances ---------------------------------------------------

static void test_ring_distances_single_ring(void) {
  std::vector<float> rings = computeRingDistances(50.0f, 1);
  TEST_ASSERT_EQUAL_size_t(1, rings.size());
  TEST_ASSERT_EQUAL_FLOAT(50.0f, rings[0]);
}

static void test_ring_distances_evenly_spaced(void) {
  std::vector<float> rings = computeRingDistances(40.0f, 4);
  TEST_ASSERT_EQUAL_size_t(4, rings.size());
  TEST_ASSERT_EQUAL_FLOAT(10.0f, rings[0]);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, rings[1]);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, rings[2]);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, rings[3]);  // last ring lands exactly on max
}

static void test_ring_distances_three_rings(void) {
  std::vector<float> rings = computeRingDistances(30.0f, 3);
  TEST_ASSERT_EQUAL_size_t(3, rings.size());
  TEST_ASSERT_EQUAL_FLOAT(10.0f, rings[0]);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, rings[1]);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, rings[2]);
}

static void test_ring_distances_zero_or_negative_count_is_empty(void) {
  TEST_ASSERT_TRUE(computeRingDistances(50.0f, 0).empty());
  TEST_ASSERT_TRUE(computeRingDistances(50.0f, -1).empty());
}

// ---- polarToScreen: Screen 3's real plot geometry ------------------------
// center (95,127), radius 85px (see CLAUDE.md "Screen 3 — Radar"). Scan
// radius is set to 85 km here so distance maps 1:1 to pixels, keeping the
// expected coordinates exact.
static constexpr int16_t kS3Cx = 95;
static constexpr int16_t kS3Cy = 127;
static constexpr int16_t kS3RadiusPx = 85;
static constexpr float kS3MaxKm = 85.0f;

static void test_s3_north_edge(void) {
  ScreenPoint p = polarToScreen(0.0f, 85.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_EQUAL_INT(95, p.x);
  TEST_ASSERT_EQUAL_INT(42, p.y);  // 127 - 85
  TEST_ASSERT_FALSE(p.clamped);
}

static void test_s3_east_edge(void) {
  ScreenPoint p = polarToScreen(90.0f, 85.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_EQUAL_INT(180, p.x);  // 95 + 85
  TEST_ASSERT_EQUAL_INT(127, p.y);
}

static void test_s3_south_edge(void) {
  ScreenPoint p = polarToScreen(180.0f, 85.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_EQUAL_INT(95, p.x);
  TEST_ASSERT_EQUAL_INT(212, p.y);  // 127 + 85
}

static void test_s3_west_edge(void) {
  ScreenPoint p = polarToScreen(270.0f, 85.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_EQUAL_INT(10, p.x);  // 95 - 85
  TEST_ASSERT_EQUAL_INT(127, p.y);
}

static void test_s3_center_when_distance_zero(void) {
  ScreenPoint p = polarToScreen(123.0f, 0.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_EQUAL_INT(95, p.x);
  TEST_ASSERT_EQUAL_INT(127, p.y);
  TEST_ASSERT_FALSE(p.clamped);
}

static void test_s3_fractional_distance_north(void) {
  // 17 km of 85 -> 17 px toward the top.
  ScreenPoint p = polarToScreen(0.0f, 17.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_EQUAL_INT(95, p.x);
  TEST_ASSERT_EQUAL_INT(110, p.y);  // 127 - 17
}

static void test_s3_diagonal_lands_on_circle(void) {
  ScreenPoint p = polarToScreen(45.0f, 85.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_INT_WITHIN(1, 155, p.x);  // 95 + 85*sin45 ~= 155
  TEST_ASSERT_INT_WITHIN(1, 67, p.y);   // 127 - 85*cos45 ~= 67
  float dx = static_cast<float>(p.x - kS3Cx);
  float dy = static_cast<float>(p.y - kS3Cy);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 85.0f, sqrtf(dx * dx + dy * dy));
}

static void test_s3_beyond_scan_radius_clamps_to_ring(void) {
  ScreenPoint p = polarToScreen(90.0f, 200.0f, kS3MaxKm, kS3Cx, kS3Cy, kS3RadiusPx);
  TEST_ASSERT_TRUE(p.clamped);
  TEST_ASSERT_EQUAL_INT(180, p.x);  // still exactly on the outer ring
  TEST_ASSERT_EQUAL_INT(127, p.y);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_bearing_0_deg_is_straight_up);
  RUN_TEST(test_bearing_90_deg_is_straight_right);
  RUN_TEST(test_bearing_180_deg_is_straight_down);
  RUN_TEST(test_bearing_270_deg_is_straight_left);

  RUN_TEST(test_several_distances_scale_linearly_along_north);

  RUN_TEST(test_zero_distance_is_center_regardless_of_bearing);

  RUN_TEST(test_beyond_max_distance_clamps_to_ring_edge);
  RUN_TEST(test_beyond_max_distance_lands_exactly_on_circle_at_any_bearing);
  RUN_TEST(test_negative_distance_does_not_invert_point);
  RUN_TEST(test_non_positive_max_distance_collapses_to_center);

  RUN_TEST(test_ring_distances_single_ring);
  RUN_TEST(test_ring_distances_evenly_spaced);
  RUN_TEST(test_ring_distances_three_rings);
  RUN_TEST(test_ring_distances_zero_or_negative_count_is_empty);

  RUN_TEST(test_s3_north_edge);
  RUN_TEST(test_s3_east_edge);
  RUN_TEST(test_s3_south_edge);
  RUN_TEST(test_s3_west_edge);
  RUN_TEST(test_s3_center_when_distance_zero);
  RUN_TEST(test_s3_fractional_distance_north);
  RUN_TEST(test_s3_diagonal_lands_on_circle);
  RUN_TEST(test_s3_beyond_scan_radius_clamps_to_ring);

  return UNITY_END();
}
