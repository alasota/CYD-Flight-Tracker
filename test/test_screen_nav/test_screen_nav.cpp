#include <unity.h>

#include "screen_nav.h"

void setUp(void) {}
void tearDown(void) {}

// The physical CYD panel in landscape (see CLAUDE.md).
static constexpr int16_t kW = 320;
static constexpr int16_t kH = 240;
// Content area bottom / visual nav-bar top (240 - LCARS_BOTTOM_NAV_HEIGHT).
static constexpr int16_t kContentBottom = 215;
// Nav-bar touch band top — 5px above the visual bar (kBottomNavTouchExtra).
static constexpr int16_t kNavTouchTop = 210;

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

// ---- ScreenNav (stateful tracker) -----------------------------------

static void test_screen_nav_defaults_to_flights(void) {
  ScreenNav nav;
  TEST_ASSERT_EQUAL_INT(kScreenFlights, nav.current());
}

static void test_screen_nav_seeds_from_initial_with_wraparound(void) {
  TEST_ASSERT_EQUAL_INT(kScreenRadar, ScreenNav(kScreenRadar).current());
  TEST_ASSERT_EQUAL_INT(kScreenFlights, ScreenNav(3).current());   // wraps
  TEST_ASSERT_EQUAL_INT(kScreenRadar, ScreenNav(-1).current());    // wraps
}

static void test_screen_nav_next_prev_report_change(void) {
  ScreenNav nav(kScreenFlights);

  TEST_ASSERT_TRUE(nav.next());
  TEST_ASSERT_EQUAL_INT(kScreenFlight, nav.current());
  TEST_ASSERT_TRUE(nav.next());
  TEST_ASSERT_EQUAL_INT(kScreenRadar, nav.current());
  TEST_ASSERT_TRUE(nav.next());  // wraps
  TEST_ASSERT_EQUAL_INT(kScreenFlights, nav.current());

  TEST_ASSERT_TRUE(nav.prev());  // wraps back
  TEST_ASSERT_EQUAL_INT(kScreenRadar, nav.current());
}

static void test_screen_nav_set_to_same_screen_reports_no_change(void) {
  ScreenNav nav(kScreenFlight);
  TEST_ASSERT_FALSE(nav.set(kScreenFlight));
  TEST_ASSERT_EQUAL_INT(kScreenFlight, nav.current());

  TEST_ASSERT_TRUE(nav.set(kScreenRadar));
  TEST_ASSERT_EQUAL_INT(kScreenRadar, nav.current());
}

static void test_screen_nav_set_wraps_garbage(void) {
  ScreenNav nav;
  TEST_ASSERT_TRUE(nav.set(7));  // 7 % 3 == 1
  TEST_ASSERT_EQUAL_INT(kScreenFlight, nav.current());
}

// ---- touch hit-testing: only the bottom buttons navigate ---------------
//
// The left/right edge strips that used to switch screens were removed on
// request — anywhere outside the bottom nav band is NavAction::None, left
// for screen-specific handling (e.g. Screen 1's table paging).

static void test_left_edge_does_not_navigate(void) {
  TEST_ASSERT_TRUE(navHitTest(5, 120, kW, kH).action == NavAction::None);
  TEST_ASSERT_TRUE(navHitTest(0, 30, kW, kH).action == NavAction::None);
}

static void test_right_edge_does_not_navigate(void) {
  TEST_ASSERT_TRUE(navHitTest(kW - 5, 120, kW, kH).action == NavAction::None);
  TEST_ASSERT_TRUE(navHitTest(kW - 1, 200, kW, kH).action == NavAction::None);
}

static void test_middle_of_content_is_none(void) {
  NavHit h = navHitTest(kW / 2, 120, kW, kH);
  TEST_ASSERT_TRUE(h.action == NavAction::None);
}

static void test_tap_in_header_is_none(void) {
  NavHit h = navHitTest(5, 10, kW, kH);
  TEST_ASSERT_TRUE(h.action == NavAction::None);
}

static void test_nothing_above_the_nav_band_navigates(void) {
  for (int16_t y = 0; y < kNavTouchTop; y += 7) {
    for (int16_t x = 0; x < kW; x += 13) {
      TEST_ASSERT_TRUE(navHitTest(x, y, kW, kH).action == NavAction::None);
    }
  }
}

static void test_nav_bar_touch_band_extends_above_the_visual_bar(void) {
  // CLAUDE.md: the bar only *draws* from y:215 but taps from y:210 count
  // as the nav bar (a little slack for an imprecise finger). A tap at y in 210..215 is
  // visually still the content area above the bar, yet hits a nav segment.
  for (int16_t y = kNavTouchTop; y < kContentBottom; ++y) {
    NavHit mid = navHitTest(160, y, kW, kH);
    TEST_ASSERT_TRUE(mid.action == NavAction::JumpTo);
    TEST_ASSERT_EQUAL_INT(1, mid.target);  // centre column -> screen 1
  }
  // ...including at the far left (x=5) — the band covers the full width.
  NavHit corner = navHitTest(5, static_cast<int16_t>(kNavTouchTop + 3), kW, kH);
  TEST_ASSERT_TRUE(corner.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(0, corner.target);
  // Just below the header, well above the band, the middle is still None.
  TEST_ASSERT_TRUE(navHitTest(160, 120, kW, kH).action == NavAction::None);
}

// ---- touch hit-testing: bottom nav segments -------------------------

static void test_bottom_nav_segments_jump_to_each_screen(void) {
  int16_t navY = static_cast<int16_t>(kH - LCARS_BOTTOM_NAV_HEIGHT / 2);  // inside the visual bar

  NavHit s0 = navHitTest(50, navY, kW, kH);
  TEST_ASSERT_TRUE(s0.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(0, s0.target);

  NavHit s1 = navHitTest(160, navY, kW, kH);
  TEST_ASSERT_TRUE(s1.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(1, s1.target);

  NavHit s2 = navHitTest(290, navY, kW, kH);
  TEST_ASSERT_TRUE(s2.action == NavAction::JumpTo);
  TEST_ASSERT_EQUAL_INT(2, s2.target);
}

static void test_bottom_nav_bar_draws_at_215_to_240(void) {
  Rect bar = bottomNavBounds(kW, kH);
  TEST_ASSERT_EQUAL_INT(kContentBottom, bar.y);          // 215
  TEST_ASSERT_EQUAL_INT(LCARS_BOTTOM_NAV_HEIGHT, bar.h);  // 25
  TEST_ASSERT_EQUAL_INT(kH, bar.y + bar.h);               // reaches the frame bottom
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

// ---- tab geometry ---------------------------------------------------------

static void test_nav_tab_rule_spans_the_bar_top(void) {
  Rect rule = navTabRule(kW, kH);
  TEST_ASSERT_EQUAL_INT(0, rule.x);
  TEST_ASSERT_EQUAL_INT(kW, rule.w);
  TEST_ASSERT_EQUAL_INT(kContentBottom, rule.y);  // inside the bar, not the content
  TEST_ASSERT_EQUAL_INT(kNavTabRulePx, rule.h);
}

static void test_active_tab_merges_into_the_rule_and_is_taller(void) {
  for (int i = 0; i < kScreenCount; ++i) {
    Rect a = navTabRect(i, true, kW, kH);
    Rect n = navTabRect(i, false, kW, kH);
    TEST_ASSERT_EQUAL_INT(kContentBottom, a.y);  // touches the rule
    TEST_ASSERT_EQUAL_INT(kH, a.y + a.h);
    TEST_ASSERT_EQUAL_INT(kH, n.y + n.h);
    TEST_ASSERT_TRUE(n.y > kContentBottom + kNavTabRulePx);  // gap below the rule
    TEST_ASSERT_TRUE(a.h > n.h);
    TEST_ASSERT_TRUE(a.w > n.w);
  }
}

static void test_nav_tabs_stay_on_screen(void) {
  for (int i = 0; i < kScreenCount; ++i) {
    for (int active = 0; active < 2; ++active) {
      Rect t = navTabRect(i, active == 1, kW, kH);
      TEST_ASSERT_TRUE(t.x >= 0);
      TEST_ASSERT_TRUE(t.x + t.w <= kW);
      TEST_ASSERT_TRUE(t.y >= kContentBottom);
      TEST_ASSERT_TRUE(t.y + t.h <= kH);
    }
  }
}

static void test_inactive_tabs_are_separated(void) {
  Rect t0 = navTabRect(0, false, kW, kH);
  Rect t1 = navTabRect(1, false, kW, kH);
  Rect t2 = navTabRect(2, false, kW, kH);
  TEST_ASSERT_TRUE(t0.x + t0.w < t1.x);
  TEST_ASSERT_TRUE(t1.x + t1.w < t2.x);
}

static void test_active_tab_overlaps_its_neighbours(void) {
  Rect a1 = navTabRect(1, true, kW, kH);
  Rect n0 = navTabRect(0, false, kW, kH);
  Rect n2 = navTabRect(2, false, kW, kH);
  TEST_ASSERT_TRUE(a1.x < n0.x + n0.w);
  TEST_ASSERT_TRUE(a1.x + a1.w > n2.x);
}

static void test_nav_tabs_fit_a_16px_label(void) {
  // LCARS_FONT_BODY (efontJA_16) plus its 1px outline must fit the shorter,
  // inactive tab; the longest label ("FLIGHTS", 7 x 8px) its width.
  for (int i = 0; i < kScreenCount; ++i) {
    Rect n = navTabRect(i, false, kW, kH);
    TEST_ASSERT_TRUE(n.h - 2 >= 16);
    TEST_ASSERT_TRUE(n.w - 2 >= 7 * 8);
  }
}

static void test_nav_tab_out_of_range_is_empty(void) {
  TEST_ASSERT_EQUAL_INT(0, navTabRect(-1, true, kW, kH).w);
  TEST_ASSERT_EQUAL_INT(0, navTabRect(3, false, kW, kH).w);
}


// ---- shouldDeferAutoSwitch: hold condition for Screen 2 ----------------
//
// defer <=> currentScreen==FLIGHT && cpaFound && -5 < tCpa < 6
// (both bounds strict — see CLAUDE.md "Auto screen cycling").

static void test_defer_only_on_flight_screen(void) {
  TEST_ASSERT_TRUE(shouldDeferAutoSwitch(kScreenFlight, true, 3.0f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlights, true, 3.0f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenRadar, true, 3.0f));
}

static void test_defer_requires_cpa_found(void) {
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, false, 3.0f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, false, 0.0f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, false, -1.0f));
}

static void test_defer_inside_window_holds(void) {
  TEST_ASSERT_TRUE(shouldDeferAutoSwitch(kScreenFlight, true, 5.9f));
  TEST_ASSERT_TRUE(shouldDeferAutoSwitch(kScreenFlight, true, 0.0f));
  TEST_ASSERT_TRUE(shouldDeferAutoSwitch(kScreenFlight, true, -4.9f));
}

static void test_defer_boundary_exactly_6_does_not_hold(void) {
  // tCpaSeconds < 6 is strict.
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, 6.0f));
  TEST_ASSERT_TRUE(shouldDeferAutoSwitch(kScreenFlight, true, 5.999f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, 6.001f));
}

static void test_defer_boundary_exactly_minus_5_does_not_hold(void) {
  // tCpaSeconds > -5 is strict.
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, -5.0f));
  TEST_ASSERT_TRUE(shouldDeferAutoSwitch(kScreenFlight, true, -4.999f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, -5.001f));
}

static void test_defer_outside_window_switches_normally(void) {
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, 30.0f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, 100.0f));
  TEST_ASSERT_FALSE(shouldDeferAutoSwitch(kScreenFlight, true, -20.0f));
}


// ---- shouldAutoAdvance: "is it time to auto-switch" -------------------
//
// advance <=> elapsedMs >= intervalS*1000 && !deferHold. This is the last
// auto-cycle-related piece that's testable without the device (main.cpp
// owns only the millis() bookkeeping around it).

static void test_auto_advance_false_before_interval(void) {
  TEST_ASSERT_FALSE(shouldAutoAdvance(0, 15, false));
  TEST_ASSERT_FALSE(shouldAutoAdvance(14999, 15, false));
}

static void test_auto_advance_true_at_and_after_interval(void) {
  TEST_ASSERT_TRUE(shouldAutoAdvance(15000, 15, false));   // exactly the interval
  TEST_ASSERT_TRUE(shouldAutoAdvance(15001, 15, false));
  TEST_ASSERT_TRUE(shouldAutoAdvance(60000, 15, false));   // long overdue
}

static void test_auto_advance_defer_hold_blocks_switch(void) {
  // deferHold true -> never advance, however overdue the timer is.
  TEST_ASSERT_FALSE(shouldAutoAdvance(15000, 15, true));
  TEST_ASSERT_FALSE(shouldAutoAdvance(600000, 15, true));
}

static void test_auto_advance_fires_immediately_once_hold_clears(void) {
  // Same elapsed time, hold just cleared -> advance now (main.cpp doesn't
  // reset its timer while deferred, so elapsed stays large).
  TEST_ASSERT_FALSE(shouldAutoAdvance(30000, 15, true));
  TEST_ASSERT_TRUE(shouldAutoAdvance(30000, 15, false));
}

static void test_auto_advance_no_overflow_on_huge_interval(void) {
  // intervalS * 1000 must not wrap uint32: 5,000,000 s * 1000 = 5e9 > 2^32.
  // 4e9 ms elapsed is still short of a 5e9 ms interval -> no advance.
  TEST_ASSERT_FALSE(shouldAutoAdvance(4000000000u, 5000000u, false));
  TEST_ASSERT_TRUE(shouldAutoAdvance(4000000000u, 3000000u, false));  // 3e9 ms interval, reached
}

static void test_persist_screen_user_change_always_writes(void) {
  // Manual tap: persist now, regardless of how recently we last wrote.
  TEST_ASSERT_TRUE(shouldPersistScreenChange(true, 0, kScreenPersistMinIntervalMs));
  TEST_ASSERT_TRUE(shouldPersistScreenChange(true, 1, kScreenPersistMinIntervalMs));
}

static void test_persist_screen_auto_change_rate_limited(void) {
  // Auto-cycle: only persist once the min interval has elapsed.
  TEST_ASSERT_FALSE(shouldPersistScreenChange(false, 0, kScreenPersistMinIntervalMs));
  TEST_ASSERT_FALSE(
      shouldPersistScreenChange(false, kScreenPersistMinIntervalMs - 1, kScreenPersistMinIntervalMs));
  TEST_ASSERT_TRUE(
      shouldPersistScreenChange(false, kScreenPersistMinIntervalMs, kScreenPersistMinIntervalMs));
  TEST_ASSERT_TRUE(shouldPersistScreenChange(false, kScreenPersistMinIntervalMs + 5000,
                                             kScreenPersistMinIntervalMs));
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

  RUN_TEST(test_screen_nav_defaults_to_flights);
  RUN_TEST(test_screen_nav_seeds_from_initial_with_wraparound);
  RUN_TEST(test_screen_nav_next_prev_report_change);
  RUN_TEST(test_screen_nav_set_to_same_screen_reports_no_change);
  RUN_TEST(test_screen_nav_set_wraps_garbage);

  RUN_TEST(test_left_edge_does_not_navigate);
  RUN_TEST(test_right_edge_does_not_navigate);
  RUN_TEST(test_middle_of_content_is_none);
  RUN_TEST(test_tap_in_header_is_none);
  RUN_TEST(test_nothing_above_the_nav_band_navigates);
  RUN_TEST(test_nav_bar_touch_band_extends_above_the_visual_bar);

  RUN_TEST(test_bottom_nav_segments_jump_to_each_screen);
  RUN_TEST(test_bottom_nav_bar_draws_at_215_to_240);
  RUN_TEST(test_bottom_nav_segments_tile_the_bar_without_gaps);
  RUN_TEST(test_bottom_nav_segment_out_of_range_is_empty);
  RUN_TEST(test_nav_tab_rule_spans_the_bar_top);
  RUN_TEST(test_active_tab_merges_into_the_rule_and_is_taller);
  RUN_TEST(test_nav_tabs_stay_on_screen);
  RUN_TEST(test_inactive_tabs_are_separated);
  RUN_TEST(test_active_tab_overlaps_its_neighbours);
  RUN_TEST(test_nav_tabs_fit_a_16px_label);
  RUN_TEST(test_nav_tab_out_of_range_is_empty);

  RUN_TEST(test_defer_only_on_flight_screen);
  RUN_TEST(test_defer_requires_cpa_found);
  RUN_TEST(test_defer_inside_window_holds);
  RUN_TEST(test_defer_boundary_exactly_6_does_not_hold);
  RUN_TEST(test_defer_boundary_exactly_minus_5_does_not_hold);
  RUN_TEST(test_defer_outside_window_switches_normally);

  RUN_TEST(test_auto_advance_false_before_interval);
  RUN_TEST(test_auto_advance_true_at_and_after_interval);
  RUN_TEST(test_auto_advance_defer_hold_blocks_switch);
  RUN_TEST(test_auto_advance_fires_immediately_once_hold_clears);
  RUN_TEST(test_auto_advance_no_overflow_on_huge_interval);

  RUN_TEST(test_persist_screen_user_change_always_writes);
  RUN_TEST(test_persist_screen_auto_change_rate_limited);

  return UNITY_END();
}
