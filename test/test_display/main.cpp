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
void run_utf8_tests();
void run_fit_to_width_tests();
void run_font_set_tests();
void run_fonts_tests();
void run_backlight_tests();
void run_i18n_tests();
void run_ui_flow_tests();
void run_update_prompt_flow_tests();
void run_ota_screen_flow_tests();
void run_alert_screen_flow_tests();
void run_password_reset_flow_tests();
void run_screenshot_bridge_tests();
void run_ui_perf_tests();

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
  if (RUN_MODULE(utf8)) run_utf8_tests();
  if (RUN_MODULE(fit_to_width)) run_fit_to_width_tests();
  if (RUN_MODULE(font_set)) run_font_set_tests();
  if (RUN_MODULE(fonts)) run_fonts_tests();
  if (RUN_MODULE(backlight)) run_backlight_tests();
  if (RUN_MODULE(i18n)) run_i18n_tests();
  if (RUN_MODULE(ui_flow)) run_ui_flow_tests();
  if (RUN_MODULE(update_prompt_flow)) run_update_prompt_flow_tests();
  if (RUN_MODULE(ota_screen_flow)) run_ota_screen_flow_tests();
  if (RUN_MODULE(alert_screen_flow)) run_alert_screen_flow_tests();
  if (RUN_MODULE(password_reset_flow)) run_password_reset_flow_tests();
  if (RUN_MODULE(screenshot_bridge)) run_screenshot_bridge_tests();
  if (RUN_MODULE(ui_perf)) run_ui_perf_tests();
  return UNITY_END();
}
