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
  // flight_phase thresholds — CLAUDE.md "Flight phase" defaults.
  TEST_ASSERT_EQUAL_FLOAT(15.0f, cfg.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, cfg.climb_rate_threshold_mps);
}

static void test_clamp_near_airport_km_floors_and_ceils(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.5f, clampNearAirportKm(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, clampNearAirportKm(-3.0f));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, clampNearAirportKm(15.0f));
  TEST_ASSERT_EQUAL_FLOAT(500.0f, clampNearAirportKm(9999.0f));
}

static void test_clamp_climb_rate_threshold_floors_and_ceils(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.1f, clampClimbRateThresholdMps(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.1f, clampClimbRateThresholdMps(-2.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.5f, clampClimbRateThresholdMps(1.5f));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, clampClimbRateThresholdMps(1000.0f));
}

static void test_sanitize_config_clamps_flight_phase_thresholds(void) {
  Config cfg;
  cfg.near_airport_km = -1.0f;
  cfg.climb_rate_threshold_mps = 0.0f;
  Config out = sanitizeConfig(cfg);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, out.climb_rate_threshold_mps);

  Config ok;
  ok.near_airport_km = 20.0f;
  ok.climb_rate_threshold_mps = 2.0f;
  Config outOk = sanitizeConfig(ok);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, outOk.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, outOk.climb_rate_threshold_mps);
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

static void test_default_language_is_polish(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL),
                        static_cast<int>(defaultConfig().language));
}

static void test_sanitize_config_preserves_valid_language(void) {
  Config en;
  en.language = Language::EN;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::EN),
                        static_cast<int>(sanitizeConfig(en).language));
}

static void test_sanitize_config_resets_invalid_language(void) {
  // e.g. a stale/corrupt NVS byte — must not index past the i18n tables.
  Config bad;
  bad.language = static_cast<Language>(7);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage),
                        static_cast<int>(sanitizeConfig(bad).language));
}

static void test_web_language_is_independent_and_sanitized(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL),
                        static_cast<int>(defaultConfig().web_language));
  Config c;
  c.language = Language::PL;
  c.web_language = Language::EN;
  Config s = sanitizeConfig(c);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL), static_cast<int>(s.language));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::EN), static_cast<int>(s.web_language));
  c.web_language = static_cast<Language>(9);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage),
                        static_cast<int>(sanitizeConfig(c).web_language));
}

static void test_web_password_defaults_to_none_and_invalid_ones_are_dropped(void) {
  TEST_ASSERT_TRUE(defaultConfig().web_password.empty());
  Config c = defaultConfig();
  c.web_password = "hunter22";
  TEST_ASSERT_EQUAL_STRING("hunter22", sanitizeConfig(c).web_password.c_str());
  c.web_password = "short";
  TEST_ASSERT_TRUE(sanitizeConfig(c).web_password.empty());
  c.web_password = "zaÅ¼Ã³Åcgesl";
  TEST_ASSERT_TRUE(sanitizeConfig(c).web_password.empty());
}

static void test_update_settings_defaults_and_sanitizing(void) {
  const Config d = defaultConfig();
  TEST_ASSERT_TRUE(d.update_check_enabled);    // checks by default, never installs by itself
  TEST_ASSERT_FALSE(d.update_show_prerelease);  // development versions only on request
  TEST_ASSERT_TRUE(d.update_skipped_tag.empty());

  Config c = defaultConfig();
  c.update_check_enabled = false;
  c.update_show_prerelease = true;
  c.update_skipped_tag = "v1.4.0";
  Config s = sanitizeConfig(c);
  TEST_ASSERT_FALSE(s.update_check_enabled);
  TEST_ASSERT_TRUE(s.update_show_prerelease);
  TEST_ASSERT_EQUAL_STRING("v1.4.0", s.update_skipped_tag.c_str());
  for (const char *bad : {"garbage", "v1.4", "v1.4.0-3-gabc1234", "<script>"}) {
    c.update_skipped_tag = bad;
    TEST_ASSERT_TRUE_MESSAGE(sanitizeConfig(c).update_skipped_tag.empty(), bad);
  }
}

// ---- Brightness / night mode (spec/03) ----------------------------------------

static void test_display_defaults(void) {
  const Config c = defaultConfig();
  TEST_ASSERT_TRUE(c.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(10, c.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(100, c.brightness_max_pct);
  TEST_ASSERT_TRUE(c.night_mode == NightMode::Sun);  // owner decision §8.2
  TEST_ASSERT_EQUAL_UINT8(10, c.night_pct);
  TEST_ASSERT_EQUAL_UINT16(60, c.night_wake_s);
  TEST_ASSERT_FALSE(c.night_pause_polling);           // owner decision §8.3
  TEST_ASSERT_EQUAL_UINT16(23 * 60, c.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(6 * 60 + 30, c.night_to_min);
}

static void test_sanitize_display_settings(void) {
  Config c = defaultConfig();
  c.brightness_fixed_pct = 0;
  c.brightness_min_pct = 90;
  c.brightness_max_pct = 200;
  c.night_mode = static_cast<NightMode>(9);
  c.night_from_min = 5000;
  c.night_pct = 150;
  c.night_wake_s = 3;
  Config s = sanitizeConfig(c);
  TEST_ASSERT_EQUAL_UINT8(5, s.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(90, s.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(100, s.brightness_max_pct);
  TEST_ASSERT_TRUE(s.night_mode == kDefaultNightMode);
  TEST_ASSERT_EQUAL_UINT16(1439, s.night_from_min);
  TEST_ASSERT_EQUAL_UINT8(100, s.night_pct);
  TEST_ASSERT_EQUAL_UINT16(10, s.night_wake_s);

  c = defaultConfig();
  c.brightness_min_pct = 80;  // min > max: swapped
  c.brightness_max_pct = 30;
  c.night_pct = 0;            // 0 = off is allowed at night
  c.night_wake_s = 9999;
  s = sanitizeConfig(c);
  TEST_ASSERT_EQUAL_UINT8(30, s.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(80, s.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT8(0, s.night_pct);
  TEST_ASSERT_EQUAL_UINT16(600, s.night_wake_s);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_web_language_is_independent_and_sanitized);
  RUN_TEST(test_web_password_defaults_to_none_and_invalid_ones_are_dropped);
  RUN_TEST(test_default_language_is_polish);
  RUN_TEST(test_sanitize_config_preserves_valid_language);
  RUN_TEST(test_sanitize_config_resets_invalid_language);
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
  RUN_TEST(test_clamp_near_airport_km_floors_and_ceils);
  RUN_TEST(test_clamp_climb_rate_threshold_floors_and_ceils);
  RUN_TEST(test_sanitize_config_clamps_flight_phase_thresholds);
  RUN_TEST(test_update_settings_defaults_and_sanitizing);
  RUN_TEST(test_display_defaults);
  RUN_TEST(test_sanitize_display_settings);
  return UNITY_END();
}
