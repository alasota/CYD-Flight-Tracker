#include <unity.h>

#include "config_store.h"

void setUp(void) {}
void tearDown(void) {}

static void test_default_config_has_sane_values(void) {
  Config cfg = defaultConfig();
  TEST_ASSERT_EQUAL_FLOAT(2.5f, cfg.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(15, cfg.poll_interval_s);
  TEST_ASSERT_TRUE(cfg.last_view == ViewMode::Table);
  TEST_ASSERT_TRUE(cfg.opensky_client_id.empty());
  TEST_ASSERT_TRUE(cfg.opensky_client_secret.empty());
}

static void test_clamp_radius_floors_and_ceils(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.1f, clampRadiusDeg(-5.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.1f, clampRadiusDeg(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(5.0f, clampRadiusDeg(5.0f));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, clampRadiusDeg(999.0f));
}

static void test_clamp_poll_interval_enforces_minimum(void) {
  TEST_ASSERT_EQUAL_UINT32(5, clampPollIntervalS(0));
  TEST_ASSERT_EQUAL_UINT32(5, clampPollIntervalS(1));
  TEST_ASSERT_EQUAL_UINT32(15, clampPollIntervalS(15));
  TEST_ASSERT_EQUAL_UINT32(3600, clampPollIntervalS(3600));
}

static void test_sanitize_config_clamps_all_fields(void) {
  Config cfg;
  cfg.home_lat = 500.0f;
  cfg.home_lon = -500.0f;
  cfg.radius_deg = -1.0f;
  cfg.poll_interval_s = 0;

  Config out = sanitizeConfig(cfg);

  TEST_ASSERT_EQUAL_FLOAT(90.0f, out.home_lat);
  TEST_ASSERT_EQUAL_FLOAT(-180.0f, out.home_lon);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, out.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(5, out.poll_interval_s);
}

static void test_sanitize_config_leaves_in_range_values_untouched(void) {
  Config cfg;
  cfg.home_lat = 51.5f;
  cfg.home_lon = -0.1f;
  cfg.radius_deg = 3.0f;
  cfg.poll_interval_s = 30;

  Config out = sanitizeConfig(cfg);

  TEST_ASSERT_EQUAL_FLOAT(51.5f, out.home_lat);
  TEST_ASSERT_EQUAL_FLOAT(-0.1f, out.home_lon);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, out.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(30, out.poll_interval_s);
}

static void test_view_mode_from_value_defaults_to_table(void) {
  TEST_ASSERT_TRUE(viewModeFromValue(0) == ViewMode::Table);
  TEST_ASSERT_TRUE(viewModeFromValue(1) == ViewMode::Radar);
  TEST_ASSERT_TRUE(viewModeFromValue(255) == ViewMode::Table);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_has_sane_values);
  RUN_TEST(test_clamp_radius_floors_and_ceils);
  RUN_TEST(test_clamp_poll_interval_enforces_minimum);
  RUN_TEST(test_sanitize_config_clamps_all_fields);
  RUN_TEST(test_sanitize_config_leaves_in_range_values_untouched);
  RUN_TEST(test_view_mode_from_value_defaults_to_table);
  return UNITY_END();
}
