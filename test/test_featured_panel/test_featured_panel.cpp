#include <unity.h>

#include "featured_panel.h"
#include "table_view.h"  // AircraftRow

void setUp(void) {}
void tearDown(void) {}

// featured_panel's text formatting now lives in aircraft_summary
// (formatSummaryIdentity / formatSummaryRoute — covered by
// test_aircraft_summary). What remains pure here is the panel's fixed
// on-frame geometry from CLAUDE.md "Screen 1" ("y: 18 to 95px").

static void test_featured_panel_top_is_18(void) {
  TEST_ASSERT_EQUAL_INT(18, featuredPanelTopPx());
}

static void test_featured_panel_height_is_77(void) {
  TEST_ASSERT_EQUAL_INT(77, featuredPanelHeightPx());
}

static void test_featured_panel_bottom_is_95_and_on_screen(void) {
  int16_t bottom = static_cast<int16_t>(featuredPanelTopPx() + featuredPanelHeightPx());
  TEST_ASSERT_EQUAL_INT(95, bottom);
  TEST_ASSERT_TRUE(bottom < 240);  // leaves room for the table section below
}

// ---- featuredPillTexts --------------------------------------------------------

static AircraftRow airborneRow() {
  AircraftRow row;
  row.aircraft.has_position = true;
  row.aircraft.on_ground = false;
  row.aircraft.baro_altitude = 1250.4f;
  row.aircraft.velocity = 103.6f;
  row.has_distance = true;
  row.distance_km = 6.2f;
  return row;
}

static void test_pills_airborne_altitude_speed_distance(void) {
  std::vector<std::string> p = featuredPillTexts(airborneRow(), Language::PL);
  TEST_ASSERT_EQUAL_size_t(3, p.size());
  TEST_ASSERT_EQUAL_STRING("1250m", p[0].c_str());
  TEST_ASSERT_EQUAL_STRING("373km/h", p[1].c_str());  // 103.6 m/s * 3.6
  TEST_ASSERT_EQUAL_STRING("6km", p[2].c_str());
}

static void test_pills_on_ground_hide_altitude_and_speed(void) {
  AircraftRow row = airborneRow();
  row.aircraft.on_ground = true;
  std::vector<std::string> pl = featuredPillTexts(row, Language::PL);
  TEST_ASSERT_EQUAL_size_t(2, pl.size());  // wide "on ground" pill + distance
  TEST_ASSERT_EQUAL_STRING("NA ZIEMI", pl[0].c_str());
  TEST_ASSERT_EQUAL_STRING("6km", pl[1].c_str());
  TEST_ASSERT_EQUAL_STRING("ON GROUND", featuredPillTexts(row, Language::EN)[0].c_str());
}

static void test_pills_without_position_show_dashes_not_zero(void) {
  AircraftRow row;  // no position, no distance
  std::vector<std::string> p = featuredPillTexts(row, Language::EN);
  TEST_ASSERT_EQUAL_size_t(3, p.size());
  for (const std::string &s : p) TEST_ASSERT_EQUAL_STRING("--", s.c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_featured_panel_top_is_18);
  RUN_TEST(test_featured_panel_height_is_77);
  RUN_TEST(test_featured_panel_bottom_is_95_and_on_screen);
  RUN_TEST(test_pills_airborne_altitude_speed_distance);
  RUN_TEST(test_pills_on_ground_hide_altitude_and_speed);
  RUN_TEST(test_pills_without_position_show_dashes_not_zero);

  return UNITY_END();
}
