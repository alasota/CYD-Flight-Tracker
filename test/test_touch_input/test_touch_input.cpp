#include <unity.h>

#include "touch_input.h"

void setUp(void) {}
void tearDown(void) {}

static Rect makeBounds() {
  Rect r;
  r.x = 10;
  r.y = 20;
  r.w = 100;
  r.h = 50;
  return r;
}

static void test_hit_inside_bounds(void) {
  Rect bounds = makeBounds();
  TEST_ASSERT_TRUE(hitTest(50, 40, bounds));
}

static void test_miss_clearly_outside_bounds(void) {
  Rect bounds = makeBounds();
  TEST_ASSERT_FALSE(hitTest(0, 0, bounds));
  TEST_ASSERT_FALSE(hitTest(500, 500, bounds));
  TEST_ASSERT_FALSE(hitTest(5, 40, bounds));    // left of bounds
  TEST_ASSERT_FALSE(hitTest(50, 200, bounds));  // below bounds
}

static void test_hit_on_edges_and_corners(void) {
  Rect bounds = makeBounds();  // x:[10,110] y:[20,70] inclusive

  // Corners.
  TEST_ASSERT_TRUE(hitTest(10, 20, bounds));    // top-left
  TEST_ASSERT_TRUE(hitTest(110, 20, bounds));   // top-right
  TEST_ASSERT_TRUE(hitTest(10, 70, bounds));    // bottom-left
  TEST_ASSERT_TRUE(hitTest(110, 70, bounds));   // bottom-right

  // Mid-edges.
  TEST_ASSERT_TRUE(hitTest(60, 20, bounds));  // top edge
  TEST_ASSERT_TRUE(hitTest(10, 45, bounds));  // left edge
}

static void test_miss_just_outside_each_edge(void) {
  Rect bounds = makeBounds();  // x:[10,110] y:[20,70] inclusive

  TEST_ASSERT_FALSE(hitTest(9, 40, bounds));    // 1px left of left edge
  TEST_ASSERT_FALSE(hitTest(111, 40, bounds));  // 1px right of right edge
  TEST_ASSERT_FALSE(hitTest(50, 19, bounds));   // 1px above top edge
  TEST_ASSERT_FALSE(hitTest(50, 71, bounds));   // 1px below bottom edge
}

static void test_hit_test_zero_size_bounds(void) {
  Rect point;
  point.x = 5;
  point.y = 5;
  point.w = 0;
  point.h = 0;

  TEST_ASSERT_TRUE(hitTest(5, 5, point));
  TEST_ASSERT_FALSE(hitTest(6, 5, point));
}

// ---- mapTouchAxis (raw ADC -> screen pixel) -----------------------------

static void test_map_touch_axis_endpoints_and_midpoint(void) {
  // Typical calibration: raw 200..3900 -> pixel 0..319.
  TEST_ASSERT_EQUAL_INT16(0, mapTouchAxis(200, 200, 3900, 0, 319));
  TEST_ASSERT_EQUAL_INT16(319, mapTouchAxis(3900, 200, 3900, 0, 319));
  TEST_ASSERT_EQUAL_INT16(159, mapTouchAxis(2050, 200, 3900, 0, 319));  // midpoint
}

static void test_map_touch_axis_clamps_out_of_range_raw(void) {
  TEST_ASSERT_EQUAL_INT16(0, mapTouchAxis(-100, 200, 3900, 0, 319));   // below inMin
  TEST_ASSERT_EQUAL_INT16(319, mapTouchAxis(9999, 200, 3900, 0, 319));  // above inMax
}

static void test_map_touch_axis_degenerate_range_does_not_divide_by_zero(void) {
  TEST_ASSERT_EQUAL_INT16(0, mapTouchAxis(1234, 500, 500, 0, 319));
}

static void test_map_touch_axis_supports_inverted_output(void) {
  // Some digitizers are wired so higher raw = lower pixel.
  TEST_ASSERT_EQUAL_INT16(319, mapTouchAxis(200, 200, 3900, 319, 0));
  TEST_ASSERT_EQUAL_INT16(0, mapTouchAxis(3900, 200, 3900, 319, 0));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_hit_inside_bounds);
  RUN_TEST(test_miss_clearly_outside_bounds);
  RUN_TEST(test_hit_on_edges_and_corners);
  RUN_TEST(test_miss_just_outside_each_edge);
  RUN_TEST(test_hit_test_zero_size_bounds);

  RUN_TEST(test_map_touch_axis_endpoints_and_midpoint);
  RUN_TEST(test_map_touch_axis_clamps_out_of_range_raw);
  RUN_TEST(test_map_touch_axis_degenerate_range_does_not_divide_by_zero);
  RUN_TEST(test_map_touch_axis_supports_inverted_output);

  return UNITY_END();
}
