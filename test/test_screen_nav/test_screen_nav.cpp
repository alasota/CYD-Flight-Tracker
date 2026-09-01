#include <unity.h>

#include "screen_nav.h"

void setUp(void) {}
void tearDown(void) {}

// The physical CYD panel in landscape (see CLAUDE.md) and the shared
// header height (LCARS_HEADER_HEIGHT == 25).
static constexpr int16_t kW = 320;
static constexpr int16_t kH = 240;
static constexpr int16_t kHeader = 25;

// ---- wraparound: forward ------------------------------------------------

static void test_next_screen_cycles_forward(void) {
  TEST_ASSERT_EQUAL_INT(kScreenFlight, nextScreen(kScreenFlights));
  TEST_ASSERT_EQUAL_INT(kScreenRadar, nextScreen(kScreenFlight));
  TEST_ASSERT_EQUAL_INT(kScreenFlights, nextScreen(kScreenRadar));  // wraps
}

// ---- wraparound: backward ---------------------------------------------

static void test_prev_screen_cycles_backward(void) {
  TEST_ASSERT_EQUAL_INT(kScreenRadar, prevScreen(kScreenFlights));  // wraps
  TEST_ASSERT_EQUAL_INT(kScreenFlights, prevScreen(kScreenFlight));
  TEST_ASSERT_EQUAL_INT(kScreenFlight, prevScreen(kScreenRadar));
}

static void test_next_then_prev_is_identity(void) {
  for (int s = 0; s < kScreenCount; ++s) {
    TEST_ASSERT_EQUAL_INT(s, prevScreen(nextScreen(s)));
    TEST_ASSERT_EQUAL_INT(s, nextScreen(prevScreen(s)));
  }
}

static void test_full_forward_cycle_returns_to_start(void) {
  int s = kScreenFlights;
  for (int i = 0; i < kScreenCount; ++i) s = nextScreen(s);
  TEST_ASSERT_EQUAL_INT(kScreenFlights, s);
}

static void test_full_backward_cycle_returns_to_start(void) {
  int s = kScreenFlights;
  for (int i = 0; i < kScreenCount; ++i) s = prevScreen(s);
  TEST_ASSERT_EQUAL_INT(kScreenFlights, s);
}

// ---- wrapScreen: out-of-range / garbage persisted values --------------

static void test_wrap_screen_normalizes_out_of_range(void) {
  TEST_ASSERT_EQUAL_INT(0, wrapScreen(3));
  TEST_ASSERT_EQUAL_INT(1, wrapScreen(4));
  TEST_ASSERT_EQUAL_INT(2, wrapScreen(200));  // 200 % 3 == 2
  TEST_ASSERT_EQUAL_INT(2, wrapScreen(-1));
  TEST_ASSERT_EQUAL_INT(0, wrapScreen(-3));
  TEST_ASSERT_EQUAL_INT(2, wrapScreen(-4));
}

static void test_wrap_screen_leaves_valid_values_untouched(void) {
  TEST_ASSERT_EQUAL_INT(0, wrapScreen(0));
  TEST_ASSERT_EQUAL_INT(1, wrapScreen(1));
  TEST_ASSERT_EQUAL_INT(2, wrapScreen(2));
}

// ---- touch hit-testing: edge strips ----------------------------------

static void test_left_edge_strip_is_prev(void) {
  // Well inside the left strip, below the header, above the nav bar.
  NavHit h = navHitTest(5, 120, kW, kH, kHeader);
  TEST_ASSERT_TRUE(h.action == NavAction::Prev);
}

static void test_right_edge_strip_is_next(void) {
  NavHit h = navHitTest(kW - 5, 120, kW, kH, kHeader);
  TEST_ASSERT_TRUE(h.action == NavAction::Next);
}

static void test_middle_of_content_is_none(void) {
  // The middle majority is deliberately left for screen-specific touch.
  NavHit h = navHitTest(kW / 2, 120, kW, kH, kHeader);
  TEST_ASSERT_TRUE(h.action == NavAction::None);
}

static void test_tap_in_header_is_none(void) {
  NavHit h = navHitTest(5, 10, kW, kH, kHeader);  // x in strip range, but y in header
  TEST_ASSERT_TRUE(h.action == NavAction::None);
}

static void test_edge_strip_is_roughly_18_percent_wide(void) {
  // 18% of 320 == 57 px. 56 is inside, 58 is out (in the middle zone).
  Rect left = leftEdgeZone(kW, kH, kHeader);
  TEST_ASSERT_EQUAL_INT(57, left.w);
  TEST_ASSERT_TRUE(navHitTest(56, 120, kW, kH, kHeader).action == NavAction::Prev);
  TEST_ASSERT_TRUE(navHitTest(58, 120, kW, kH, kHeader).action == NavAction::None);
}

static void test_edge_strips_stop_above_the_nav_bar(void) {
  // y within the bottom nav bar but x in the left strip range -> the nav
  // bar wins (it's a JumpTo), not Prev.
  int16_t navY = static_cast<int16_t>(kH - kBottomNavHeight + 2);
  NavHit h = navHitTest(5, navY, kW, kH, kHeader);
  TEST_ASSERT_TRUE(h.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(0, h.target);
}

// ---- touch hit-testing: bottom nav segments -------------------------

static void test_bottom_nav_segments_jump_to_each_screen(void) {
  int16_t navY = static_cast<int16_t>(kH - kBottomNavHeight / 2);

  NavHit s0 = navHitTest(50, navY, kW, kH, kHeader);
  TEST_ASSERT_TRUE(s0.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(0, s0.target);

  NavHit s1 = navHitTest(160, navY, kW, kH, kHeader);
  TEST_ASSERT_TRUE(s1.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(1, s1.target);

  NavHit s2 = navHitTest(290, navY, kW, kH, kHeader);
  TEST_ASSERT_TRUE(s2.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(2, s2.target);
}

static void test_bottom_nav_segments_tile_the_bar_without_gaps(void) {
  Rect bar = bottomNavBounds(kW, kH);
  Rect s0 = bottomNavSegment(0, kW, kH);
  Rect s1 = bottomNavSegment(1, kW, kH);
  Rect s2 = bottomNavSegment(2, kW, kH);

  TEST_ASSERT_EQUAL_INT(0, s0.x);
  TEST_ASSERT_EQUAL_INT(s0.x + s0.w, s1.x);           // no gap / overlap
  TEST_ASSERT_EQUAL_INT(s1.x + s1.w, s2.x);
  TEST_ASSERT_EQUAL_INT(bar.x + bar.w, s2.x + s2.w);  // last segment reaches the edge
  TEST_ASSERT_EQUAL_INT(bar.y, s0.y);
  TEST_ASSERT_EQUAL_INT(bar.h, s0.h);
}

static void test_bottom_nav_segment_out_of_range_is_empty(void) {
  Rect r = bottomNavSegment(5, kW, kH);
  TEST_ASSERT_EQUAL_INT(0, r.w);
  TEST_ASSERT_EQUAL_INT(0, r.h);
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_next_screen_cycles_forward);
  RUN_TEST(test_prev_screen_cycles_backward);
  RUN_TEST(test_next_then_prev_is_identity);
  RUN_TEST(test_full_forward_cycle_returns_to_start);
  RUN_TEST(test_full_backward_cycle_returns_to_start);

  RUN_TEST(test_wrap_screen_normalizes_out_of_range);
  RUN_TEST(test_wrap_screen_leaves_valid_values_untouched);

  RUN_TEST(test_left_edge_strip_is_prev);
  RUN_TEST(test_right_edge_strip_is_next);
  RUN_TEST(test_middle_of_content_is_none);
  RUN_TEST(test_tap_in_header_is_none);
  RUN_TEST(test_edge_strip_is_roughly_18_percent_wide);
  RUN_TEST(test_edge_strips_stop_above_the_nav_bar);

  RUN_TEST(test_bottom_nav_segments_jump_to_each_screen);
  RUN_TEST(test_bottom_nav_segments_tile_the_bar_without_gaps);
  RUN_TEST(test_bottom_nav_segment_out_of_range_is_empty);

  return UNITY_END();
}
