#include <unity.h>

#include "cpa_predictor.h"
#include "flight_screen.h"

void setUp(void) {}
void tearDown(void) {}

static CpaPrediction cpaAt(float t) {
  CpaPrediction p;
  p.found = true;
  p.t_cpa_seconds = t;
  return p;
}

// ---- fixed zone geometry (CLAUDE.md "Screen 2") --------------------------

static void test_zone_geometry_matches_spec(void) {
  TEST_ASSERT_EQUAL_INT(28, flightIdentityTopPx());
  TEST_ASSERT_EQUAL_INT(50, flightIdentityHeightPx());
  TEST_ASSERT_EQUAL_INT(82, flightCountdownTopPx());
  TEST_ASSERT_EQUAL_INT(113, flightCountdownHeightPx());
  TEST_ASSERT_EQUAL_INT(200, flightStatusTopPx());
  TEST_ASSERT_EQUAL_INT(38, flightStatusHeightPx());
}

static void test_zones_do_not_overlap_and_fit_on_screen(void) {
  TEST_ASSERT_TRUE(flightIdentityTopPx() + flightIdentityHeightPx() <= flightCountdownTopPx());
  TEST_ASSERT_TRUE(flightCountdownTopPx() + flightCountdownHeightPx() <= flightStatusTopPx());
  TEST_ASSERT_TRUE(flightStatusTopPx() + flightStatusHeightPx() <= 240);
  TEST_ASSERT_TRUE(flightIdentityTopPx() >= 25);  // clears the status_bar header
}

// ---- computeCountdownDisplay: mode + colour + label per zone ------------

static void test_empty_when_not_found(void) {
  CountdownDisplay cd = computeCountdownDisplay(CpaPrediction{});  // found == false
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Empty);
  TEST_ASSERT_EQUAL_STRING("--", cd.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("BRAK LOTOW W ZASIEGU", cd.statusLabel.c_str());
  TEST_ASSERT_FALSE(cd.colorCoded);
}

static void test_empty_when_more_than_10s_past(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-10.001f));
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Empty);
  TEST_ASSERT_FALSE(cd.colorCoded);
}

static void test_passed_zone_orange(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-3.0f));
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_HEX16(LCARS_ORANGE, cd.color);
  TEST_ASSERT_EQUAL_STRING("-3", cd.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("s", cd.suffixText.c_str());
  TEST_ASSERT_EQUAL_STRING("MINAL", cd.statusLabel.c_str());
  TEST_ASSERT_TRUE(cd.colorCoded);
}

static void test_passed_zone_includes_exactly_minus_10(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-10.0f));
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_HEX16(LCARS_ORANGE, cd.color);
  TEST_ASSERT_EQUAL_STRING("-10", cd.bigText.c_str());
}

static void test_imminent_zone_yellow(void) {
  CountdownDisplay lo = computeCountdownDisplay(cpaAt(0.0f));
  CountdownDisplay hi = computeCountdownDisplay(cpaAt(10.0f));  // 10 is inclusive-yellow
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, lo.color);
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, hi.color);
  TEST_ASSERT_EQUAL_STRING("0", lo.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("NAD TOBA", hi.statusLabel.c_str());
}

static void test_approaching_zone_cyan(void) {
  CountdownDisplay just_over = computeCountdownDisplay(cpaAt(10.001f));
  CountdownDisplay at_60 = computeCountdownDisplay(cpaAt(60.0f));  // 60 is inclusive-cyan
  TEST_ASSERT_EQUAL_HEX16(LCARS_CYAN, just_over.color);
  TEST_ASSERT_EQUAL_HEX16(LCARS_CYAN, at_60.color);
  TEST_ASSERT_TRUE(at_60.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_STRING("ZBLIZA SIE", just_over.statusLabel.c_str());
}

static void test_minutes_mode_magenta_over_60(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(60.001f));
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Minutes);
  TEST_ASSERT_EQUAL_HEX16(LCARS_MAGENTA, cd.color);
  TEST_ASSERT_EQUAL_STRING("MIN", cd.suffixText.c_str());
  TEST_ASSERT_EQUAL_STRING("SZACOWANY CZAS", cd.statusLabel.c_str());
}

static void test_minutes_rounds_to_nearest_minute(void) {
  TEST_ASSERT_EQUAL_STRING("~1", computeCountdownDisplay(cpaAt(61.0f)).bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("~2", computeCountdownDisplay(cpaAt(90.0f)).bigText.c_str());  // 1.5 -> 2
  TEST_ASSERT_EQUAL_STRING("~4", computeCountdownDisplay(cpaAt(240.0f)).bigText.c_str());
}

// ---- the four TEMP-harness preview values (main.cpp) --------------------

static void test_the_four_preview_values(void) {
  CountdownDisplay a = computeCountdownDisplay(cpaAt(45.0f));
  TEST_ASSERT_EQUAL_HEX16(LCARS_CYAN, a.color);
  TEST_ASSERT_EQUAL_STRING("45", a.bigText.c_str());

  CountdownDisplay b = computeCountdownDisplay(cpaAt(5.0f));
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, b.color);
  TEST_ASSERT_EQUAL_STRING("5", b.bigText.c_str());

  CountdownDisplay c = computeCountdownDisplay(cpaAt(-3.0f));
  TEST_ASSERT_EQUAL_HEX16(LCARS_ORANGE, c.color);

  CountdownDisplay d = computeCountdownDisplay(cpaAt(240.0f));
  TEST_ASSERT_EQUAL_HEX16(LCARS_MAGENTA, d.color);
  TEST_ASSERT_TRUE(d.mode == CountdownDisplay::Mode::Minutes);
}

static void test_rounding_to_nearest_second(void) {
  TEST_ASSERT_EQUAL_STRING("42", computeCountdownDisplay(cpaAt(42.4f)).bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("43", computeCountdownDisplay(cpaAt(42.6f)).bigText.c_str());
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_zone_geometry_matches_spec);
  RUN_TEST(test_zones_do_not_overlap_and_fit_on_screen);

  RUN_TEST(test_empty_when_not_found);
  RUN_TEST(test_empty_when_more_than_10s_past);
  RUN_TEST(test_passed_zone_orange);
  RUN_TEST(test_passed_zone_includes_exactly_minus_10);
  RUN_TEST(test_imminent_zone_yellow);
  RUN_TEST(test_approaching_zone_cyan);
  RUN_TEST(test_minutes_mode_magenta_over_60);
  RUN_TEST(test_minutes_rounds_to_nearest_minute);
  RUN_TEST(test_the_four_preview_values);
  RUN_TEST(test_rounding_to_nearest_second);

  return UNITY_END();
}
