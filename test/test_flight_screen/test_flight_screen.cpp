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
  TEST_ASSERT_EQUAL_INT(18, flightIdentityTopPx());
  TEST_ASSERT_EQUAL_INT(50, flightIdentityHeightPx());
  TEST_ASSERT_EQUAL_INT(72, flightCountdownTopPx());
  TEST_ASSERT_EQUAL_INT(113, flightCountdownHeightPx());
  TEST_ASSERT_EQUAL_INT(190, flightStatusTopPx());
  TEST_ASSERT_EQUAL_INT(25, flightStatusHeightPx());  // y:190..215
}

static void test_zones_do_not_overlap_and_fit_on_screen(void) {
  TEST_ASSERT_TRUE(flightIdentityTopPx() + flightIdentityHeightPx() <= flightCountdownTopPx());
  TEST_ASSERT_TRUE(flightCountdownTopPx() + flightCountdownHeightPx() <= flightStatusTopPx());
  // Status strip ends at the content boundary (240 - 25px bottom nav).
  TEST_ASSERT_TRUE(flightStatusTopPx() + flightStatusHeightPx() <= 215);
  TEST_ASSERT_TRUE(flightIdentityTopPx() >= 15);  // clears the status_bar header
}

// ---- computeCountdownDisplay: mode + colour + label per zone ------------

static void test_empty_when_not_found(void) {
  CountdownDisplay cd = computeCountdownDisplay(CpaPrediction{}, Language::PL);  // found == false
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Empty);
  TEST_ASSERT_EQUAL_STRING("--", cd.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("BRAK LOTOW W ZASIEGU", cd.statusLabel.c_str());
  TEST_ASSERT_FALSE(cd.colorCoded);
}

static void test_empty_when_more_than_10s_past(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-10.001f), Language::PL);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Empty);
  TEST_ASSERT_FALSE(cd.colorCoded);
}

static void test_passed_zone_orange(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-3.0f), Language::PL);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, cd.color);
  TEST_ASSERT_EQUAL_STRING("-3", cd.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("s", cd.suffixText.c_str());
  TEST_ASSERT_EQUAL_STRING("MINAL", cd.statusLabel.c_str());
  TEST_ASSERT_TRUE(cd.colorCoded);
}

static void test_passed_zone_includes_exactly_minus_10(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-10.0f), Language::PL);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, cd.color);
  TEST_ASSERT_EQUAL_STRING("-10", cd.bigText.c_str());
}

static void test_imminent_zone_yellow(void) {
  CountdownDisplay lo = computeCountdownDisplay(cpaAt(0.0f), Language::PL);
  CountdownDisplay hi = computeCountdownDisplay(cpaAt(10.0f), Language::PL);  // 10 is inclusive-yellow
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, lo.color);
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, hi.color);
  TEST_ASSERT_EQUAL_STRING("0", lo.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("NAD TOBA", hi.statusLabel.c_str());
}

static void test_approaching_zone_cyan(void) {
  CountdownDisplay just_over = computeCountdownDisplay(cpaAt(10.001f), Language::PL);
  CountdownDisplay at_60 = computeCountdownDisplay(cpaAt(60.0f), Language::PL);  // 60 is inclusive-cyan
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, just_over.color);
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, at_60.color);
  TEST_ASSERT_TRUE(at_60.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_STRING("ZBLIZA SIE", just_over.statusLabel.c_str());
}

static void test_minutes_mode_magenta_over_60(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(60.001f), Language::PL);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Minutes);
  TEST_ASSERT_EQUAL_HEX16(LCARS_LILAC, cd.color);
  TEST_ASSERT_EQUAL_STRING("MIN", cd.suffixText.c_str());
  TEST_ASSERT_EQUAL_STRING("SZACOWANY CZAS", cd.statusLabel.c_str());
}

static void test_minutes_rounds_to_nearest_minute(void) {
  TEST_ASSERT_EQUAL_STRING("~1", computeCountdownDisplay(cpaAt(61.0f), Language::PL).bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("~2", computeCountdownDisplay(cpaAt(90.0f), Language::PL).bigText.c_str());  // 1.5 -> 2
  TEST_ASSERT_EQUAL_STRING("~4", computeCountdownDisplay(cpaAt(240.0f), Language::PL).bigText.c_str());
}

// ---- representative countdown values: colour + text spot-checks ---------

static void test_the_four_preview_values(void) {
  CountdownDisplay a = computeCountdownDisplay(cpaAt(45.0f), Language::PL);
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, a.color);
  TEST_ASSERT_EQUAL_STRING("45", a.bigText.c_str());

  CountdownDisplay b = computeCountdownDisplay(cpaAt(5.0f), Language::PL);
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, b.color);
  TEST_ASSERT_EQUAL_STRING("5", b.bigText.c_str());

  CountdownDisplay c = computeCountdownDisplay(cpaAt(-3.0f), Language::PL);
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, c.color);

  CountdownDisplay d = computeCountdownDisplay(cpaAt(240.0f), Language::PL);
  TEST_ASSERT_EQUAL_HEX16(LCARS_LILAC, d.color);
  TEST_ASSERT_TRUE(d.mode == CountdownDisplay::Mode::Minutes);
}

static void test_rounding_to_nearest_second(void) {
  TEST_ASSERT_EQUAL_STRING("42", computeCountdownDisplay(cpaAt(42.4f), Language::PL).bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("43", computeCountdownDisplay(cpaAt(42.6f), Language::PL).bigText.c_str());
}

static void test_labels_follow_language(void) {
  TEST_ASSERT_EQUAL_STRING("NO FLIGHTS IN RANGE",
                           computeCountdownDisplay(CpaPrediction{}, Language::EN).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("APPROACHING",
                           computeCountdownDisplay(cpaAt(30.0f), Language::EN).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("OVERHEAD",
                           computeCountdownDisplay(cpaAt(5.0f), Language::EN).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("PASSED",
                           computeCountdownDisplay(cpaAt(-3.0f), Language::EN).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("ESTIMATED TIME",
                           computeCountdownDisplay(cpaAt(240.0f), Language::EN).statusLabel.c_str());
}

static void test_numbers_and_colors_do_not_depend_on_language(void) {
  for (float t : {-5.0f, 0.0f, 7.0f, 42.0f, 240.0f}) {
    CountdownDisplay pl = computeCountdownDisplay(cpaAt(t), Language::PL);
    CountdownDisplay en = computeCountdownDisplay(cpaAt(t), Language::EN);
    TEST_ASSERT_EQUAL_STRING(pl.bigText.c_str(), en.bigText.c_str());
    TEST_ASSERT_EQUAL_HEX16(pl.color, en.color);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pl.mode), static_cast<int>(en.mode));
  }
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_labels_follow_language);
  RUN_TEST(test_numbers_and_colors_do_not_depend_on_language);

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
