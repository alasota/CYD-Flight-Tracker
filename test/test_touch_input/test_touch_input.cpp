#include <unity.h>

#include <initializer_list>

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

// ---- mapRawTouch: calibration against real taps on this board ---------------
//
// Raw readings logged on the device (2026-10-17) while tapping the three
// bottom-nav buttons (drawn y:215..240, segments x:0..106 / 106..213 /
// 213..320) and the left edge mid-height. The first calibration mapped the
// buttons to y~20 — the vertical axis is inverted on this digitizer.
//
// The samples were logged with the old XPT2046_Touchscreen library
// (rotation 1). LovyanGFX reports the channels swapped AND the horizontal
// one inverted: LovyanGFX raw X = old raw y, LovyanGFX raw Y = 4095 - old
// raw x. (The first port forgot the inversion and mirrored the buttons —
// see the LovyanGFX-native samples below.) The helper takes the samples
// exactly as logged and converts them.

static TouchXY mapLoggedSample(int32_t oldRawX, int32_t oldRawY) {
  return mapRawTouch(/*rawX=*/oldRawY, /*rawY=*/4095 - oldRawX);
}

// Raw samples logged through LovyanGFX itself (2026-10-18), with the
// screen the user reported each tap opened: ~3250 opened RADAR instead of
// LOTY, i.e. it is the LEFT button. These pin the horizontal direction.
static void test_lovyangfx_native_samples_hit_the_right_buttons(void) {
  TouchXY left = mapRawTouch(585, 3288);
  TouchXY left2 = mapRawTouch(504, 3216);
  TouchXY middle = mapRawTouch(531, 1975);
  TouchXY right = mapRawTouch(530, 696);
  TouchXY right2 = mapRawTouch(649, 844);
  TEST_ASSERT_TRUE_MESSAGE(left.x < 106, "raw Y ~3290 must be the LEFT (LOTY) button");
  TEST_ASSERT_TRUE(left2.x < 106);
  TEST_ASSERT_TRUE(middle.x >= 106 && middle.x < 213);
  TEST_ASSERT_TRUE_MESSAGE(right.x >= 213, "raw Y ~700 must be the RIGHT (RADAR) button");
  TEST_ASSERT_TRUE(right2.x >= 213);
  for (const TouchXY &p : {left, left2, middle, right, right2}) TEST_ASSERT_TRUE(p.y >= 210);
}

static void assertInNavSegment(int32_t rawX, int32_t rawY, int16_t segMinX, int16_t segMaxX) {
  TouchXY p = mapLoggedSample(rawX, rawY);
  TEST_ASSERT_TRUE_MESSAGE(p.y >= 210, "bottom-nav tap must land in the nav touch band");
  TEST_ASSERT_TRUE(p.y <= 239);
  TEST_ASSERT_TRUE(p.x >= segMinX && p.x < segMaxX);
}

static void test_logged_left_button_taps(void) {
  assertInNavSegment(976, 660, 0, 106);
  assertInNavSegment(885, 537, 0, 106);
  assertInNavSegment(746, 545, 0, 106);
}

static void test_logged_middle_button_taps(void) {
  assertInNavSegment(1980, 554, 106, 213);
  assertInNavSegment(2131, 589, 106, 213);
  assertInNavSegment(1899, 459, 106, 213);
}

static void test_logged_right_button_taps(void) {
  assertInNavSegment(3287, 618, 213, 320);
  assertInNavSegment(3012, 405, 213, 320);
  assertInNavSegment(2949, 528, 213, 320);
}

static void test_logged_left_edge_mid_height_taps(void) {
  // Near the left edge (x < 57), somewhere mid-height in the content area.
  TouchXY p = mapLoggedSample(550, 2100);
  TEST_ASSERT_TRUE(p.x < 57);
  TEST_ASSERT_TRUE(p.y > 60 && p.y < 180);
}

static void test_map_raw_touch_stays_on_screen(void) {
  for (int32_t r : {0, 200, 2000, 3900, 4095}) {
    TouchXY p = mapRawTouch(r, r);
    TEST_ASSERT_TRUE(p.x >= 0 && p.x <= 319);
    TEST_ASSERT_TRUE(p.y >= 0 && p.y <= 239);
  }
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_lovyangfx_native_samples_hit_the_right_buttons);
  RUN_TEST(test_logged_left_button_taps);
  RUN_TEST(test_logged_middle_button_taps);
  RUN_TEST(test_logged_right_button_taps);
  RUN_TEST(test_logged_left_edge_mid_height_taps);
  RUN_TEST(test_map_raw_touch_stays_on_screen);

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
