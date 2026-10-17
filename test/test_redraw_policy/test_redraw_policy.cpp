#include <unity.h>

#include "redraw_policy.h"
#include "screen_nav.h"  // kScreen* indices

void setUp(void) {}
void tearDown(void) {}

static bool has(uint8_t mask, uint8_t bit) { return (mask & bit) != 0; }

// ---- nothing happened -----------------------------------------------------

static void test_no_events_no_redraw(void) {
  RedrawEvents e;
  TEST_ASSERT_EQUAL_UINT8(0, computeRedrawMask(e, kScreenFlights));
  TEST_ASSERT_EQUAL_UINT8(0, computeRedrawMask(e, kScreenFlight));
  TEST_ASSERT_EQUAL_UINT8(0, computeRedrawMask(e, kScreenRadar));
}

// ---- full redraw only on structural changes -------------------------------

static void test_first_draw_is_full(void) {
  RedrawEvents e;
  e.firstDraw = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawFull, computeRedrawMask(e, kScreenFlights));
}

static void test_screen_change_is_full(void) {
  RedrawEvents e;
  e.screenChanged = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawFull, computeRedrawMask(e, kScreenRadar));
}

static void test_page_change_is_full(void) {
  RedrawEvents e;
  e.pageChanged = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawFull, computeRedrawMask(e, kScreenFlights));
}

static void test_language_change_is_full(void) {
  // Every label on every part of the screen changes.
  RedrawEvents e;
  e.languageChanged = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawFull, computeRedrawMask(e, kScreenFlight));
}

static void test_full_swallows_everything_else(void) {
  RedrawEvents e;
  e.screenChanged = true;
  e.newPoll = true;
  e.minuteChanged = true;
  e.healthChanged = true;
  e.countdownTick = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawFull, computeRedrawMask(e, kScreenFlight));
}

// ---- the countdown tick must never trigger a full redraw (the flicker bug) --

static void test_countdown_tick_on_flight_screen_redraws_countdown_only(void) {
  RedrawEvents e;
  e.countdownTick = true;
  uint8_t mask = computeRedrawMask(e, kScreenFlight);
  TEST_ASSERT_EQUAL_UINT8(kRedrawCountdown, mask);
  TEST_ASSERT_FALSE(has(mask, kRedrawFull));
  TEST_ASSERT_FALSE(has(mask, kRedrawContent));
}

static void test_countdown_tick_ignored_off_flight_screen(void) {
  RedrawEvents e;
  e.countdownTick = true;
  TEST_ASSERT_EQUAL_UINT8(0, computeRedrawMask(e, kScreenFlights));
  TEST_ASSERT_EQUAL_UINT8(0, computeRedrawMask(e, kScreenRadar));
}

// ---- new data: content area only -------------------------------------------

static void test_new_poll_redraws_content_and_header_not_full(void) {
  RedrawEvents e;
  e.newPoll = true;
  for (int screen = 0; screen < kScreenCount; ++screen) {
    uint8_t mask = computeRedrawMask(e, screen);
    TEST_ASSERT_TRUE(has(mask, kRedrawContent));
    TEST_ASSERT_FALSE(has(mask, kRedrawFull));
    TEST_ASSERT_TRUE(has(mask, kRedrawHeader));  // aircraft count in the header block
  }
}

static void test_new_poll_plus_tick_on_flight_needs_no_separate_countdown(void) {
  // Content already includes the countdown zone.
  RedrawEvents e;
  e.newPoll = true;
  e.countdownTick = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawContent | kRedrawHeader, computeRedrawMask(e, kScreenFlight));
}

// ---- header: minute rollover / health (block colour) ------------------------

static void test_minute_change_redraws_header_text_only(void) {
  RedrawEvents e;
  e.minuteChanged = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawHeader, computeRedrawMask(e, kScreenRadar));
}

static void test_health_change_redraws_header_text_only(void) {
  RedrawEvents e;
  e.healthChanged = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawHeader, computeRedrawMask(e, kScreenFlights));
}

static void test_independent_regions_combine(void) {
  RedrawEvents e;
  e.minuteChanged = true;
  e.countdownTick = true;
  TEST_ASSERT_EQUAL_UINT8(kRedrawHeader | kRedrawCountdown,
                          computeRedrawMask(e, kScreenFlight));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_no_events_no_redraw);

  RUN_TEST(test_first_draw_is_full);
  RUN_TEST(test_screen_change_is_full);
  RUN_TEST(test_page_change_is_full);
  RUN_TEST(test_language_change_is_full);
  RUN_TEST(test_full_swallows_everything_else);

  RUN_TEST(test_countdown_tick_on_flight_screen_redraws_countdown_only);
  RUN_TEST(test_countdown_tick_ignored_off_flight_screen);

  RUN_TEST(test_new_poll_redraws_content_and_header_not_full);
  RUN_TEST(test_new_poll_plus_tick_on_flight_needs_no_separate_countdown);

  RUN_TEST(test_minute_change_redraws_header_text_only);
  RUN_TEST(test_health_change_redraws_header_text_only);
  RUN_TEST(test_independent_regions_combine);

  return UNITY_END();
}
