#include <unity.h>

#include "cpa_predictor.h"
#include "flight_screen.h"
#include "table_view.h"  // AircraftRow

#include <vector>

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
  TEST_ASSERT_EQUAL_INT(70, flightIdentityHeightPx());  // 3 lines: identity, route, stats
  TEST_ASSERT_EQUAL_INT(92, flightCountdownTopPx());
  TEST_ASSERT_EQUAL_INT(94, flightCountdownHeightPx());  // still fits the ~75px digits
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
  CountdownDisplay cd = computeCountdownDisplay(CpaPrediction{}, Language::PL, false);  // found == false
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Empty);
  TEST_ASSERT_EQUAL_STRING("--", cd.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("BRAK LOTÓW W ZASIĘGU", cd.statusLabel.c_str());
  TEST_ASSERT_FALSE(cd.colorCoded);
}

static void test_empty_when_more_than_10s_past(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-10.001f), Language::PL, true);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Empty);
  TEST_ASSERT_FALSE(cd.colorCoded);
}

static void test_passed_zone_orange(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-3.0f), Language::PL, true);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, cd.color);
  TEST_ASSERT_EQUAL_STRING("-3", cd.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("s", cd.suffixText.c_str());
  TEST_ASSERT_EQUAL_STRING("MINĄŁ", cd.statusLabel.c_str());
  TEST_ASSERT_TRUE(cd.colorCoded);
}

static void test_passed_zone_includes_exactly_minus_10(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(-10.0f), Language::PL, true);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, cd.color);
  TEST_ASSERT_EQUAL_STRING("-10", cd.bigText.c_str());
}

static void test_imminent_zone_yellow(void) {
  CountdownDisplay lo = computeCountdownDisplay(cpaAt(0.0f), Language::PL, true);
  CountdownDisplay hi = computeCountdownDisplay(cpaAt(10.0f), Language::PL, true);  // 10 is inclusive-yellow
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, lo.color);
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, hi.color);
  TEST_ASSERT_EQUAL_STRING("0", lo.bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("NAD TOBĄ", hi.statusLabel.c_str());
}

static void test_approaching_zone_cyan(void) {
  CountdownDisplay just_over = computeCountdownDisplay(cpaAt(10.001f), Language::PL, true);
  CountdownDisplay at_60 = computeCountdownDisplay(cpaAt(60.0f), Language::PL, true);  // 60 is inclusive-cyan
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, just_over.color);
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, at_60.color);
  TEST_ASSERT_TRUE(at_60.mode == CountdownDisplay::Mode::Seconds);
  TEST_ASSERT_EQUAL_STRING("ZBLIŻA SIĘ", just_over.statusLabel.c_str());
}

static void test_minutes_mode_magenta_over_60(void) {
  CountdownDisplay cd = computeCountdownDisplay(cpaAt(60.001f), Language::PL, true);
  TEST_ASSERT_TRUE(cd.mode == CountdownDisplay::Mode::Minutes);
  TEST_ASSERT_EQUAL_HEX16(LCARS_LILAC, cd.color);
  TEST_ASSERT_EQUAL_STRING("MIN", cd.suffixText.c_str());
  TEST_ASSERT_EQUAL_STRING("SZACOWANY CZAS", cd.statusLabel.c_str());
}

static void test_minutes_rounds_to_nearest_minute(void) {
  TEST_ASSERT_EQUAL_STRING("~1", computeCountdownDisplay(cpaAt(61.0f), Language::PL, true).bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("~2", computeCountdownDisplay(cpaAt(90.0f), Language::PL, true).bigText.c_str());  // 1.5 -> 2
  TEST_ASSERT_EQUAL_STRING("~4", computeCountdownDisplay(cpaAt(240.0f), Language::PL, true).bigText.c_str());
}

// ---- representative countdown values: colour + text spot-checks ---------

static void test_the_four_preview_values(void) {
  CountdownDisplay a = computeCountdownDisplay(cpaAt(45.0f), Language::PL, true);
  TEST_ASSERT_EQUAL_HEX16(LCARS_SKY, a.color);
  TEST_ASSERT_EQUAL_STRING("45", a.bigText.c_str());

  CountdownDisplay b = computeCountdownDisplay(cpaAt(5.0f), Language::PL, true);
  TEST_ASSERT_EQUAL_HEX16(LCARS_YELLOW, b.color);
  TEST_ASSERT_EQUAL_STRING("5", b.bigText.c_str());

  CountdownDisplay c = computeCountdownDisplay(cpaAt(-3.0f), Language::PL, true);
  TEST_ASSERT_EQUAL_HEX16(LCARS_PEACH, c.color);

  CountdownDisplay d = computeCountdownDisplay(cpaAt(240.0f), Language::PL, true);
  TEST_ASSERT_EQUAL_HEX16(LCARS_LILAC, d.color);
  TEST_ASSERT_TRUE(d.mode == CountdownDisplay::Mode::Minutes);
}

static void test_rounding_to_nearest_second(void) {
  TEST_ASSERT_EQUAL_STRING("42", computeCountdownDisplay(cpaAt(42.4f), Language::PL, true).bigText.c_str());
  TEST_ASSERT_EQUAL_STRING("43", computeCountdownDisplay(cpaAt(42.6f), Language::PL, true).bigText.c_str());
}

static void test_labels_follow_language(void) {
  TEST_ASSERT_EQUAL_STRING("NO FLIGHTS IN RANGE",
                           computeCountdownDisplay(CpaPrediction{}, Language::EN, false).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("APPROACHING",
                           computeCountdownDisplay(cpaAt(30.0f), Language::EN, true).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("OVERHEAD",
                           computeCountdownDisplay(cpaAt(5.0f), Language::EN, true).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("PASSED",
                           computeCountdownDisplay(cpaAt(-3.0f), Language::EN, true).statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("ESTIMATED TIME",
                           computeCountdownDisplay(cpaAt(240.0f), Language::EN, true).statusLabel.c_str());
}

static void test_numbers_and_colors_do_not_depend_on_language(void) {
  for (float t : {-5.0f, 0.0f, 7.0f, 42.0f, 240.0f}) {
    CountdownDisplay pl = computeCountdownDisplay(cpaAt(t), Language::PL, true);
    CountdownDisplay en = computeCountdownDisplay(cpaAt(t), Language::EN, true);
    TEST_ASSERT_EQUAL_STRING(pl.bigText.c_str(), en.bigText.c_str());
    TEST_ASSERT_EQUAL_HEX16(pl.color, en.color);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pl.mode), static_cast<int>(en.mode));
  }
}

// ---- honest empty states ----------------------------------------------------

static void test_empty_with_aircraft_says_not_overhead(void) {
  // Aircraft are around, but none will pass overhead — "no flights in
  // range" (what the first version said) was simply false.
  CountdownDisplay pl = computeCountdownDisplay(CpaPrediction{}, Language::PL, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CountdownDisplay::Mode::Empty), static_cast<int>(pl.mode));
  TEST_ASSERT_EQUAL_STRING("NIE PRZELECI NAD TOBĄ", pl.statusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("NOT PASSING OVERHEAD",
                           computeCountdownDisplay(CpaPrediction{}, Language::EN, true).statusLabel.c_str());
}

static void test_empty_without_aircraft_says_no_flights(void) {
  TEST_ASSERT_EQUAL_STRING("BRAK LOTÓW W ZASIĘGU",
                           computeCountdownDisplay(CpaPrediction{}, Language::PL, false).statusLabel.c_str());
}

// ---- pickNowAircraft: the one that will be overhead soonest ----------------

static AircraftRow airborne() {
  AircraftRow r;
  r.aircraft.has_position = true;
  r.aircraft.on_ground = false;
  return r;
}

static CpaPrediction cpa(float t, float missKm) {
  CpaPrediction p;
  p.found = true;
  p.t_cpa_seconds = t;
  p.miss_distance_km = missKm;
  return p;
}

static void test_pick_prefers_soonest_overhead_not_nearest(void) {
  // Row 0 is nearest but passes 12 km away; row 1 will be overhead in 40 s.
  std::vector<AircraftRow> rows = {airborne(), airborne(), airborne()};
  std::vector<CpaPrediction> cpas = {cpa(20.0f, 12.0f), cpa(40.0f, 1.0f), cpa(90.0f, 0.5f)};
  NowPick p = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  TEST_ASSERT_EQUAL_INT(1, p.index);
  TEST_ASSERT_TRUE(p.cpa.found);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, p.cpa.t_cpa_seconds);
}

static void test_pick_skips_ground_traffic_and_long_gone(void) {
  std::vector<AircraftRow> rows = {airborne(), airborne(), airborne()};
  rows[0].aircraft.on_ground = true;  // taxiing at the airport
  std::vector<CpaPrediction> cpas = {cpa(5.0f, 0.2f), cpa(-30.0f, 0.5f), cpa(70.0f, 2.0f)};
  NowPick p = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  TEST_ASSERT_EQUAL_INT(2, p.index);
}

static void test_pick_just_passed_still_counts(void) {
  // Within the -10 s "MINĄŁ" window, the one overhead right now wins.
  std::vector<AircraftRow> rows = {airborne(), airborne()};
  std::vector<CpaPrediction> cpas = {cpa(50.0f, 1.0f), cpa(-4.0f, 0.3f)};
  TEST_ASSERT_EQUAL_INT(1, pickNowAircraft(rows, cpas, kOverheadRadiusKm).index);
}

static void test_pick_without_overhead_candidate_shows_nearest_airborne(void) {
  std::vector<AircraftRow> rows = {airborne(), airborne()};
  rows[0].aircraft.on_ground = true;
  std::vector<CpaPrediction> cpas = {cpa(5.0f, 30.0f), cpa(20.0f, 25.0f)};
  NowPick p = pickNowAircraft(rows, cpas, kOverheadRadiusKm);
  TEST_ASSERT_EQUAL_INT(1, p.index);  // first airborne row (rows are nearest-first)
  TEST_ASSERT_FALSE(p.cpa.found);     // -> "NIE PRZELECI NAD TOBĄ"
}

static void test_pick_empty(void) {
  std::vector<AircraftRow> rows;
  std::vector<CpaPrediction> cpas;
  TEST_ASSERT_EQUAL_INT(-1, pickNowAircraft(rows, cpas, kOverheadRadiusKm).index);
}

// ---- nowStatsText ---------------------------------------------------------------

static void test_now_stats_airborne(void) {
  AircraftRow r = airborne();
  r.has_distance = true;
  r.distance_km = 3.2f;
  r.aircraft.baro_altitude = 236.0f;
  r.aircraft.has_altitude = true;
  r.aircraft.velocity = 74.0f;  // 266 km/h
  r.aircraft.has_velocity = true;
  TEST_ASSERT_EQUAL_STRING("N 3km  236m  266km/h", nowStatsText(r, Language::PL).c_str());
}

static void test_now_stats_on_ground(void) {
  AircraftRow r = airborne();
  r.aircraft.on_ground = true;
  r.has_distance = true;
  r.distance_km = 6.0f;
  TEST_ASSERT_EQUAL_STRING("N 6km  NA ZIEMI", nowStatsText(r, Language::PL).c_str());
}

// ---- caption above the digits (review #10) ----------------------------------

static void test_caption_only_while_the_overflight_is_ahead(void) {
  TEST_ASSERT_EQUAL_STRING("NAD TOBĄ ZA",
                           computeCountdownDisplay(cpaAt(30.0f), Language::PL, true).caption.c_str());
  TEST_ASSERT_EQUAL_STRING("NAD TOBĄ ZA",
                           computeCountdownDisplay(cpaAt(240.0f), Language::PL, true).caption.c_str());
  TEST_ASSERT_EQUAL_STRING("OVERHEAD IN",
                           computeCountdownDisplay(cpaAt(5.0f), Language::EN, true).caption.c_str());
  // Passed / no prediction: "overhead in -3 s" would be nonsense.
  TEST_ASSERT_EQUAL_STRING("", computeCountdownDisplay(cpaAt(-3.0f), Language::PL, true).caption.c_str());
  TEST_ASSERT_EQUAL_STRING("", computeCountdownDisplay(CpaPrediction{}, Language::PL, true).caption.c_str());
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_caption_only_while_the_overflight_is_ahead);

  RUN_TEST(test_empty_with_aircraft_says_not_overhead);
  RUN_TEST(test_empty_without_aircraft_says_no_flights);
  RUN_TEST(test_pick_prefers_soonest_overhead_not_nearest);
  RUN_TEST(test_pick_skips_ground_traffic_and_long_gone);
  RUN_TEST(test_pick_just_passed_still_counts);
  RUN_TEST(test_pick_without_overhead_candidate_shows_nearest_airborne);
  RUN_TEST(test_pick_empty);
  RUN_TEST(test_now_stats_airborne);
  RUN_TEST(test_now_stats_on_ground);

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
