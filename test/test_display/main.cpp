// Suite test_display: screens, theme, navigation, touch, brightness, display texts. See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_table_view_tests();
void run_featured_panel_tests();
void run_flight_screen_tests();
void run_radar_view_tests();
void run_radar_geometry_tests();
void run_aircraft_summary_tests();
void run_status_bar_tests();
void run_service_screen_tests();
void run_screen_nav_tests();
void run_redraw_policy_tests();
void run_touch_input_tests();
void run_lcars_theme_tests();
void run_fonts_tests();
void run_backlight_tests();
void run_i18n_tests();

void setUp(void) { resetAllForTesting(); }
void tearDown(void) {}

// -DTEST_MODULE=<module> runs one module's tests (test/README.md). Not
// TEST_ONLY as spec 07 first said: unity.h already defines TEST_ONLY().
#ifdef TEST_MODULE
#define RUN_MODULE(m) testModuleIs(TEST_SUPPORT_STR(TEST_MODULE), #m)
#else
#define RUN_MODULE(m) true
#endif

int main(int, char **) {
  UNITY_BEGIN();
  if (RUN_MODULE(table_view)) run_table_view_tests();
  if (RUN_MODULE(featured_panel)) run_featured_panel_tests();
  if (RUN_MODULE(flight_screen)) run_flight_screen_tests();
  if (RUN_MODULE(radar_view)) run_radar_view_tests();
  if (RUN_MODULE(radar_geometry)) run_radar_geometry_tests();
  if (RUN_MODULE(aircraft_summary)) run_aircraft_summary_tests();
  if (RUN_MODULE(status_bar)) run_status_bar_tests();
  if (RUN_MODULE(service_screen)) run_service_screen_tests();
  if (RUN_MODULE(screen_nav)) run_screen_nav_tests();
  if (RUN_MODULE(redraw_policy)) run_redraw_policy_tests();
  if (RUN_MODULE(touch_input)) run_touch_input_tests();
  if (RUN_MODULE(lcars_theme)) run_lcars_theme_tests();
  if (RUN_MODULE(fonts)) run_fonts_tests();
  if (RUN_MODULE(backlight)) run_backlight_tests();
  if (RUN_MODULE(i18n)) run_i18n_tests();
  return UNITY_END();
}
