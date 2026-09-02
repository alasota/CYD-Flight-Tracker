#include <unity.h>

#include "config_store.h"

void setUp(void) {}
void tearDown(void) {}

static void test_default_config_has_sane_values(void) {
  Config cfg = defaultConfig();
  TEST_ASSERT_EQUAL_FLOAT(2.5f, cfg.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(15, cfg.poll_interval_s);
  TEST_ASSERT_EQUAL_INT(0, cfg.last_screen);  // Flights
  TEST_ASSERT_FALSE(cfg.auto_cycle_enabled);            // off by default
  TEST_ASSERT_EQUAL_UINT32(15, cfg.auto_cycle_interval_s);
  TEST_ASSERT_TRUE(cfg.opensky_client_id.empty());
  TEST_ASSERT_TRUE(cfg.opensky_client_secret.empty());
  // Never-configured by default — see review notes 1.5: (0,0) is a real
  // place, so this flag is the only way to tell "not set up yet" apart
  // from "home really is at 0,0".
  TEST_ASSERT_FALSE(cfg.home_configured);
}

static void test_sanitize_config_preserves_home_configured_flag(void) {
  Config configured;
  configured.home_configured = true;
  TEST_ASSERT_TRUE(sanitizeConfig(configured).home_configured);

  Config unconfigured;
  unconfigured.home_configured = false;
  TEST_ASSERT_FALSE(sanitizeConfig(unconfigured).home_configured);
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

static void test_clamp_last_screen_accepts_0_1_2(void) {
  TEST_ASSERT_EQUAL_INT(0, clampLastScreen(0));
  TEST_ASSERT_EQUAL_INT(1, clampLastScreen(1));
  TEST_ASSERT_EQUAL_INT(2, clampLastScreen(2));
}

static void test_clamp_last_screen_falls_back_to_flights(void) {
  TEST_ASSERT_EQUAL_INT(0, clampLastScreen(-1));
  TEST_ASSERT_EQUAL_INT(0, clampLastScreen(3));
  TEST_ASSERT_EQUAL_INT(0, clampLastScreen(255));
}

static void test_sanitize_config_clamps_last_screen(void) {
  Config cfg;
  cfg.last_screen = 9;
  TEST_ASSERT_EQUAL_INT(0, sanitizeConfig(cfg).last_screen);

  Config ok;
  ok.last_screen = 2;
  TEST_ASSERT_EQUAL_INT(2, sanitizeConfig(ok).last_screen);
}


static void test_clamp_auto_cycle_interval_enforces_minimum(void) {
  // CLAUDE.md "Auto screen cycling": user-configurable, but must be > 0 —
  // and in practice not so short the screen flips faster than the eye can
  // follow. Floor is 3s; anything below snaps up to it.
  TEST_ASSERT_EQUAL_UINT32(3, clampAutoCycleIntervalS(0));
  TEST_ASSERT_EQUAL_UINT32(3, clampAutoCycleIntervalS(1));
  TEST_ASSERT_EQUAL_UINT32(3, clampAutoCycleIntervalS(3));
  TEST_ASSERT_EQUAL_UINT32(15, clampAutoCycleIntervalS(15));   // the default
  TEST_ASSERT_EQUAL_UINT32(3600, clampAutoCycleIntervalS(3600));  // no ceiling
}

static void test_sanitize_config_clamps_auto_cycle_interval(void) {
  Config cfg;
  cfg.auto_cycle_interval_s = 0;
  TEST_ASSERT_EQUAL_UINT32(3, sanitizeConfig(cfg).auto_cycle_interval_s);

  Config ok;
  ok.auto_cycle_interval_s = 30;
  TEST_ASSERT_EQUAL_UINT32(30, sanitizeConfig(ok).auto_cycle_interval_s);
}

static void test_sanitize_config_preserves_auto_cycle_enabled(void) {
  Config on;
  on.auto_cycle_enabled = true;
  TEST_ASSERT_TRUE(sanitizeConfig(on).auto_cycle_enabled);

  Config off;
  off.auto_cycle_enabled = false;
  TEST_ASSERT_FALSE(sanitizeConfig(off).auto_cycle_enabled);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_has_sane_values);
  RUN_TEST(test_sanitize_config_preserves_home_configured_flag);
  RUN_TEST(test_clamp_radius_floors_and_ceils);
  RUN_TEST(test_clamp_poll_interval_enforces_minimum);
  RUN_TEST(test_sanitize_config_clamps_all_fields);
  RUN_TEST(test_sanitize_config_leaves_in_range_values_untouched);
  RUN_TEST(test_clamp_last_screen_accepts_0_1_2);
  RUN_TEST(test_clamp_last_screen_falls_back_to_flights);
  RUN_TEST(test_sanitize_config_clamps_last_screen);
  RUN_TEST(test_clamp_auto_cycle_interval_enforces_minimum);
  RUN_TEST(test_sanitize_config_clamps_auto_cycle_interval);
  RUN_TEST(test_sanitize_config_preserves_auto_cycle_enabled);
  return UNITY_END();
}
