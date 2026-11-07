// config_store: defaults, sanitizing, NVS keys. CLAUDE.md "Code conventions" (config_store).
#include <unity.h>

#include "admin/core/config_store.h"
#include "test_support.h"

namespace {

static void test_default_config_has_sane_values(void) {
  Settings cfg = defaultSettings();
  TEST_ASSERT_EQUAL_FLOAT(2.5f, cfg.home.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(15, cfg.poll.interval_s);
  TEST_ASSERT_EQUAL_INT(0, cfg.display.last_screen);  // Flights
  TEST_ASSERT_FALSE(cfg.auto_cycle.enabled);            // off by default
  TEST_ASSERT_EQUAL_UINT32(15, cfg.auto_cycle.interval_s);
  TEST_ASSERT_TRUE(cfg.opensky.client_id.empty());
  TEST_ASSERT_TRUE(cfg.opensky.client_secret.empty());
  // Never-configured by default — see docs/review-notes-2026.md 1.5: (0,0) is a real
  // place, so this flag is the only way to tell "not set up yet" apart
  // from "home really is at 0,0".
  TEST_ASSERT_FALSE(cfg.home.configured);
  // flight_phase thresholds — docs/screens.md "Flight phase" defaults.
  TEST_ASSERT_EQUAL_FLOAT(15.0f, cfg.phase.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, cfg.phase.climb_rate_threshold_mps);
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
  Settings cfg;
  cfg.phase.near_airport_km = -1.0f;
  cfg.phase.climb_rate_threshold_mps = 0.0f;
  Settings out = sanitizeSettings(cfg);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out.phase.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, out.phase.climb_rate_threshold_mps);

  Settings ok;
  ok.phase.near_airport_km = 20.0f;
  ok.phase.climb_rate_threshold_mps = 2.0f;
  Settings outOk = sanitizeSettings(ok);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, outOk.phase.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, outOk.phase.climb_rate_threshold_mps);
}

static void test_sanitize_config_preserves_home_configured_flag(void) {
  Settings configured;
  configured.home.configured = true;
  TEST_ASSERT_TRUE(sanitizeSettings(configured).home.configured);

  Settings unconfigured;
  unconfigured.home.configured = false;
  TEST_ASSERT_FALSE(sanitizeSettings(unconfigured).home.configured);
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

// Scenario: sanitizeSettings() przycina te same pola do tych samych granic, teraz w obiektach
// kontekstów (an out-of-range value in every object: home, poll, auto-cycle, phase, backlight,
// display, web).
static void test_sanitize_config_clamps_all_fields(void) {
  Settings cfg;
  cfg.home.lat = 200.0f;
  cfg.home.lon = -500.0f;
  cfg.home.radius_deg = 0.0f;
  cfg.poll.interval_s = 1;
  cfg.auto_cycle.interval_s = 0;
  cfg.phase.near_airport_km = 0.0f;
  cfg.phase.climb_rate_threshold_mps = 0.0f;
  cfg.backlight.brightness_fixed_pct = 0;
  cfg.backlight.brightness_max_pct = 200;
  cfg.backlight.night_from_min = 2000;
  cfg.backlight.night_to_min = 2000;
  cfg.display.last_screen = 7;
  cfg.display.language = static_cast<Language>(9);
  cfg.web.language = static_cast<Language>(9);

  Settings out = sanitizeSettings(cfg);

  TEST_ASSERT_EQUAL_FLOAT(90.0f, out.home.lat);
  TEST_ASSERT_EQUAL_FLOAT(-180.0f, out.home.lon);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, out.home.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(5, out.poll.interval_s);
  TEST_ASSERT_EQUAL_UINT32(3, out.auto_cycle.interval_s);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, out.phase.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, out.phase.climb_rate_threshold_mps);
  TEST_ASSERT_EQUAL_UINT8(5, out.backlight.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(100, out.backlight.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT16(1439, out.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(1439, out.backlight.night_to_min);
  TEST_ASSERT_EQUAL_INT(0, out.display.last_screen);
  ASSERT_ENUM_EQ(kDefaultLanguage, out.display.language);
  ASSERT_ENUM_EQ(kDefaultLanguage, out.web.language);
}

static void test_sanitize_config_leaves_in_range_values_untouched(void) {
  Settings cfg;
  cfg.home.lat = 51.5f;
  cfg.home.lon = -0.1f;
  cfg.home.radius_deg = 3.0f;
  cfg.poll.interval_s = 30;
  cfg.auto_cycle.interval_s = 20;
  cfg.phase.near_airport_km = 20.0f;
  cfg.phase.climb_rate_threshold_mps = 2.0f;
  cfg.backlight.brightness_fixed_pct = 42;
  cfg.backlight.brightness_max_pct = 90;
  cfg.backlight.night_from_min = 22 * 60;
  cfg.backlight.night_to_min = 5 * 60;
  cfg.display.last_screen = 2;
  cfg.display.language = Language::EN;
  cfg.web.language = Language::EN;

  Settings out = sanitizeSettings(cfg);

  TEST_ASSERT_EQUAL_FLOAT(51.5f, out.home.lat);
  TEST_ASSERT_EQUAL_FLOAT(-0.1f, out.home.lon);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, out.home.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(30, out.poll.interval_s);
  TEST_ASSERT_EQUAL_UINT32(20, out.auto_cycle.interval_s);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, out.phase.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, out.phase.climb_rate_threshold_mps);
  TEST_ASSERT_EQUAL_UINT8(42, out.backlight.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(90, out.backlight.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT16(22 * 60, out.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(5 * 60, out.backlight.night_to_min);
  TEST_ASSERT_EQUAL_INT(2, out.display.last_screen);
  ASSERT_ENUM_EQ(Language::EN, out.display.language);
  ASSERT_ENUM_EQ(Language::EN, out.web.language);
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
  Settings cfg;
  cfg.display.last_screen = 9;
  TEST_ASSERT_EQUAL_INT(0, sanitizeSettings(cfg).display.last_screen);

  Settings ok;
  ok.display.last_screen = 2;
  TEST_ASSERT_EQUAL_INT(2, sanitizeSettings(ok).display.last_screen);
}

static void test_clamp_auto_cycle_interval_enforces_minimum(void) {
  // docs/screens.md "Auto screen cycling": user-configurable, but must be > 0 —
  // and in practice not so short the screen flips faster than the eye can
  // follow. Floor is 3s; anything below snaps up to it.
  TEST_ASSERT_EQUAL_UINT32(3, clampAutoCycleIntervalS(0));
  TEST_ASSERT_EQUAL_UINT32(3, clampAutoCycleIntervalS(1));
  TEST_ASSERT_EQUAL_UINT32(3, clampAutoCycleIntervalS(3));
  TEST_ASSERT_EQUAL_UINT32(15, clampAutoCycleIntervalS(15));   // the default
  TEST_ASSERT_EQUAL_UINT32(3600, clampAutoCycleIntervalS(3600));  // no ceiling
}

static void test_sanitize_config_clamps_auto_cycle_interval(void) {
  Settings cfg;
  cfg.auto_cycle.interval_s = 0;
  TEST_ASSERT_EQUAL_UINT32(3, sanitizeSettings(cfg).auto_cycle.interval_s);

  Settings ok;
  ok.auto_cycle.interval_s = 30;
  TEST_ASSERT_EQUAL_UINT32(30, sanitizeSettings(ok).auto_cycle.interval_s);
}

static void test_sanitize_config_preserves_auto_cycle_enabled(void) {
  Settings on;
  on.auto_cycle.enabled = true;
  TEST_ASSERT_TRUE(sanitizeSettings(on).auto_cycle.enabled);

  Settings off;
  off.auto_cycle.enabled = false;
  TEST_ASSERT_FALSE(sanitizeSettings(off).auto_cycle.enabled);
}

static void test_default_language_is_polish(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL),
                        static_cast<int>(defaultSettings().display.language));
}

static void test_sanitize_config_preserves_valid_language(void) {
  Settings en;
  en.display.language = Language::EN;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::EN),
                        static_cast<int>(sanitizeSettings(en).display.language));
}

static void test_sanitize_config_resets_invalid_language(void) {
  // e.g. a stale/corrupt NVS byte — must not index past the i18n tables.
  Settings bad;
  bad.display.language = static_cast<Language>(7);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage),
                        static_cast<int>(sanitizeSettings(bad).display.language));
}

static void test_web_language_is_independent_and_sanitized(void) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL),
                        static_cast<int>(defaultSettings().web.language));
  Settings c;
  c.display.language = Language::PL;
  c.web.language = Language::EN;
  Settings s = sanitizeSettings(c);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::PL), static_cast<int>(s.display.language));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Language::EN), static_cast<int>(s.web.language));
  c.web.language = static_cast<Language>(9);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kDefaultLanguage),
                        static_cast<int>(sanitizeSettings(c).web.language));
}

static void test_web_password_defaults_to_none_and_invalid_ones_are_dropped(void) {
  TEST_ASSERT_TRUE(defaultSettings().web.password.empty());
  Settings c = defaultSettings();
  c.web.password = "hunter22";
  TEST_ASSERT_EQUAL_STRING("hunter22", sanitizeSettings(c).web.password.c_str());
  c.web.password = "short";
  TEST_ASSERT_TRUE(sanitizeSettings(c).web.password.empty());
  c.web.password = "zaÅ¼Ã³Åcgesl";
  TEST_ASSERT_TRUE(sanitizeSettings(c).web.password.empty());
}

static void test_update_settings_defaults_and_sanitizing(void) {
  const Settings d = defaultSettings();
  TEST_ASSERT_TRUE(d.update.check_enabled);    // checks by default, never installs by itself
  TEST_ASSERT_FALSE(d.update.show_prerelease);  // development versions only on request
  TEST_ASSERT_TRUE(d.update.skipped_tag.empty());

  Settings c = defaultSettings();
  c.update.check_enabled = false;
  c.update.show_prerelease = true;
  c.update.skipped_tag = "v1.4.0";
  Settings s = sanitizeSettings(c);
  TEST_ASSERT_FALSE(s.update.check_enabled);
  TEST_ASSERT_TRUE(s.update.show_prerelease);
  TEST_ASSERT_EQUAL_STRING("v1.4.0", s.update.skipped_tag.c_str());
  for (const char *bad : {"garbage", "v1.4", "v1.4.0-3-gabc1234", "<script>"}) {
    c.update.skipped_tag = bad;
    TEST_ASSERT_TRUE_MESSAGE(sanitizeSettings(c).update.skipped_tag.empty(), bad);
  }
}

// ---- Brightness / night mode (spec/03) ----------------------------------------

static void test_display_defaults(void) {
  const Settings c = defaultSettings();
  TEST_ASSERT_TRUE(c.backlight.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(10, c.backlight.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(100, c.backlight.brightness_max_pct);
  ASSERT_ENUM_EQ(NightMode::Sun, c.backlight.night_mode);  // owner decision §8.2
  TEST_ASSERT_EQUAL_UINT8(10, c.backlight.night_pct);
  TEST_ASSERT_EQUAL_UINT16(60, c.backlight.night_wake_s);
  TEST_ASSERT_FALSE(c.backlight.night_pause_polling);           // owner decision §8.3
  TEST_ASSERT_EQUAL_UINT16(23 * 60, c.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(6 * 60 + 30, c.backlight.night_to_min);
}

static void test_sanitize_display_settings(void) {
  Settings c = defaultSettings();
  c.backlight.brightness_fixed_pct = 0;
  c.backlight.brightness_min_pct = 90;
  c.backlight.brightness_max_pct = 200;
  c.backlight.night_mode = static_cast<NightMode>(9);
  c.backlight.night_from_min = 5000;
  c.backlight.night_pct = 150;
  c.backlight.night_wake_s = 3;
  Settings s = sanitizeSettings(c);
  TEST_ASSERT_EQUAL_UINT8(5, s.backlight.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(90, s.backlight.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(100, s.backlight.brightness_max_pct);
  ASSERT_ENUM_EQ(kDefaultNightMode, s.backlight.night_mode);
  TEST_ASSERT_EQUAL_UINT16(1439, s.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT8(100, s.backlight.night_pct);
  TEST_ASSERT_EQUAL_UINT16(10, s.backlight.night_wake_s);

  c = defaultSettings();
  c.backlight.brightness_min_pct = 80;  // min > max: swapped
  c.backlight.brightness_max_pct = 30;
  c.backlight.night_pct = 0;            // 0 = off is allowed at night
  c.backlight.night_wake_s = 9999;
  s = sanitizeSettings(c);
  TEST_ASSERT_EQUAL_UINT8(30, s.backlight.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(80, s.backlight.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT8(0, s.backlight.night_pct);
  TEST_ASSERT_EQUAL_UINT16(600, s.backlight.night_wake_s);
}

// Scenario: Wartości domyślne ustawień są te same co przed etapem — every field of every
// object against its v1.11.3 default.
static void test_default_config_keeps_every_stage_5_default(void) {
  const Settings c = defaultSettings();
  // home
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.home.lat);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.home.lon);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, c.home.radius_deg);
  TEST_ASSERT_FALSE(c.home.configured);
  // phase
  TEST_ASSERT_EQUAL_FLOAT(15.0f, c.phase.near_airport_km);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, c.phase.climb_rate_threshold_mps);
  // opensky, poll
  TEST_ASSERT_TRUE(c.opensky.client_id.empty());
  TEST_ASSERT_TRUE(c.opensky.client_secret.empty());
  TEST_ASSERT_EQUAL_UINT32(15, c.poll.interval_s);
  // display, auto_cycle
  ASSERT_ENUM_EQ(Language::PL, c.display.language);
  TEST_ASSERT_EQUAL_INT(0, c.display.last_screen);
  TEST_ASSERT_FALSE(c.auto_cycle.enabled);
  TEST_ASSERT_EQUAL_UINT32(15, c.auto_cycle.interval_s);
  // backlight
  TEST_ASSERT_TRUE(c.backlight.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(80, c.backlight.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(10, c.backlight.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(100, c.backlight.brightness_max_pct);
  ASSERT_ENUM_EQ(NightMode::Sun, c.backlight.night_mode);
  ASSERT_ENUM_EQ(kDefaultNightMode, c.backlight.night_mode);
  TEST_ASSERT_EQUAL_UINT16(23 * 60, c.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(6 * 60 + 30, c.backlight.night_to_min);
  TEST_ASSERT_EQUAL_UINT8(10, c.backlight.night_pct);
  TEST_ASSERT_EQUAL_UINT16(60, c.backlight.night_wake_s);
  TEST_ASSERT_FALSE(c.backlight.night_pause_polling);
  // update
  TEST_ASSERT_TRUE(c.update.check_enabled);
  TEST_ASSERT_FALSE(c.update.show_prerelease);
  TEST_ASSERT_TRUE(c.update.skipped_tag.empty());
  // web
  ASSERT_ENUM_EQ(Language::PL, c.web.language);
  TEST_ASSERT_TRUE(c.web.password.empty());
}

}  // namespace

void run_config_store_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
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
  RUN_TEST(test_default_config_keeps_every_stage_5_default);
}
