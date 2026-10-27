// backlight: auto brightness, night window, touch wake. docs/architecture.md "Brightness & night mode", spec/03.
#include <unity.h>

#include "display/domain/backlight.h"
#include "test_support.h"

namespace {

// ---- LightFilter --------------------------------------------------------------

static void test_filter_drops_single_spikes(void) {
  LightFilter f;
  for (int i = 0; i < 30; ++i) f.add(i % 7 == 3 ? 150 : 0);  // spikes as seen in a lit room
  TEST_ASSERT_TRUE(f.ready());
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, f.average());
}

static void test_filter_needs_warmup_then_follows(void) {
  LightFilter f;
  for (int i = 0; i < kLightWarmupSamples - 1; ++i) f.add(1200);
  TEST_ASSERT_FALSE(f.ready());
  f.add(1200);
  TEST_ASSERT_TRUE(f.ready());
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 1200.0f, f.average());
  for (int i = 0; i < 60; ++i) f.add(0);  // lights on: ~6 s later it's bright
  TEST_ASSERT_TRUE(f.average() < 50.0f);
  TEST_ASSERT_EQUAL_UINT16(0, f.last());
}

static void test_filter_keeps_ready_after_many_samples(void) {
  LightFilter f;
  for (int i = 0; i < 5000; ++i) f.add(500);
  TEST_ASSERT_TRUE(f.ready());
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 500.0f, f.average());
}

// ---- autoBrightnessPct -----------------------------------------------------------

static void test_auto_curve_ends(void) {
  TEST_ASSERT_EQUAL_UINT8(100, autoBrightnessPct(0, 10, 100, 0));
  TEST_ASSERT_EQUAL_UINT8(100, autoBrightnessPct(kLightRawBright, 10, 100, 0));
  TEST_ASSERT_EQUAL_UINT8(10, autoBrightnessPct(kLightRawDark, 10, 100, 0));
  TEST_ASSERT_EQUAL_UINT8(10, autoBrightnessPct(4095, 10, 100, 0));
}

static void test_auto_curve_middle_and_swapped_limits(void) {
  const float mid = (kLightRawBright + kLightRawDark) / 2.0f;
  TEST_ASSERT_EQUAL_UINT8(55, autoBrightnessPct(mid, 10, 100, 0));
  TEST_ASSERT_EQUAL_UINT8(55, autoBrightnessPct(mid, 100, 10, 0));
}

static void test_auto_curve_hysteresis(void) {
  const float mid = (kLightRawBright + kLightRawDark) / 2.0f;  // 55 %
  TEST_ASSERT_EQUAL_UINT8(52, autoBrightnessPct(mid, 10, 100, 52));  // 3 % off: kept
  TEST_ASSERT_EQUAL_UINT8(55, autoBrightnessPct(mid, 10, 100, 60));  // 5 %: moves
  // The ends are always reachable, even within the hysteresis.
  TEST_ASSERT_EQUAL_UINT8(100, autoBrightnessPct(0, 10, 100, 97));
  TEST_ASSERT_EQUAL_UINT8(10, autoBrightnessPct(4095, 10, 100, 12));
  // A previous value outside new limits isn't kept.
  TEST_ASSERT_EQUAL_UINT8(80, autoBrightnessPct(0, 10, 80, 100));
}

// ---- pctToPwm / stepPwm --------------------------------------------------------

static void test_pct_to_pwm(void) {
  TEST_ASSERT_EQUAL_UINT8(0, pctToPwm(0));
  TEST_ASSERT_EQUAL_UINT8(kMinVisiblePwm, pctToPwm(1));
  TEST_ASSERT_EQUAL_UINT8(255, pctToPwm(100));
  TEST_ASSERT_EQUAL_UINT8(255, pctToPwm(150));
  TEST_ASSERT_TRUE(pctToPwm(50) < 128);  // perceptual: half brightness < half duty
  for (int p = 1; p < 100; ++p) {
    TEST_ASSERT_TRUE(pctToPwm(static_cast<uint8_t>(p)) <= pctToPwm(static_cast<uint8_t>(p + 1)));
  }
}

static void test_step_pwm(void) {
  TEST_ASSERT_EQUAL_UINT8(200, stepPwm(10, 200, 50, 0));     // no ramp: jump
  TEST_ASSERT_EQUAL_UINT8(22, stepPwm(10, 200, 50, 1000));   // 255 * 50/1000 = 12
  TEST_ASSERT_EQUAL_UINT8(188, stepPwm(200, 10, 50, 1000));
  TEST_ASSERT_EQUAL_UINT8(11, stepPwm(10, 200, 1, 3000));    // at least 1
  TEST_ASSERT_EQUAL_UINT8(200, stepPwm(195, 200, 100, 1000)); // never overshoots
  TEST_ASSERT_EQUAL_UINT8(0, stepPwm(3, 0, 100, 1000));
  TEST_ASSERT_EQUAL_UINT8(77, stepPwm(77, 77, 100, 1000));
}

// ---- Night window ------------------------------------------------------------------

static void test_window_through_midnight(void) {
  const int from = 23 * 60, to = 6 * 60 + 30;
  TEST_ASSERT_TRUE(inNightWindow(23 * 60, from, to));
  TEST_ASSERT_TRUE(inNightWindow(0, from, to));
  TEST_ASSERT_TRUE(inNightWindow(6 * 60 + 29, from, to));
  TEST_ASSERT_FALSE(inNightWindow(6 * 60 + 30, from, to));
  TEST_ASSERT_FALSE(inNightWindow(12 * 60, from, to));
  TEST_ASSERT_FALSE(inNightWindow(22 * 60 + 59, from, to));
}

static void test_window_within_a_day_and_empty(void) {
  TEST_ASSERT_TRUE(inNightWindow(60, 60, 300));
  TEST_ASSERT_FALSE(inNightWindow(300, 60, 300));
  TEST_ASSERT_FALSE(inNightWindow(30, 60, 300));
  TEST_ASSERT_FALSE(inNightWindow(100, 100, 100));
}

static void test_window_for_each_mode(void) {
  SunTimes sun;
  sun.sunrise_min = 7 * 60 + 17;
  sun.sunset_min = 17 * 60 + 23;
  NightWindow w = nightWindowFor(NightMode::Off, 1380, 390, sun, true);
  ASSERT_ENUM_EQ(NightWindow::Kind::Never, w.kind);
  TEST_ASSERT_EQUAL_INT(0, nightWindowMinutes(w));

  w = nightWindowFor(NightMode::Schedule, 1380, 390, sun, true);
  TEST_ASSERT_TRUE(isNight(w, 0));
  TEST_ASSERT_FALSE(isNight(w, 12 * 60));
  TEST_ASSERT_EQUAL_INT(450, nightWindowMinutes(w));  // 7.5 h

  w = nightWindowFor(NightMode::Sun, 1380, 390, sun, true);
  TEST_ASSERT_TRUE(isNight(w, 18 * 60));
  TEST_ASSERT_TRUE(isNight(w, 7 * 60));
  TEST_ASSERT_FALSE(isNight(w, 7 * 60 + 17));
  TEST_ASSERT_FALSE(isNight(w, 17 * 60 + 22));
  TEST_ASSERT_EQUAL_INT(1440 - (17 * 60 + 23) + 7 * 60 + 17, nightWindowMinutes(w));

  // Sun unknown (no location / clock) -> never; polar day / night.
  ASSERT_ENUM_EQ(NightWindow::Kind::Never, nightWindowFor(NightMode::Sun, 0, 0, sun, false).kind);
  sun.kind = SunTimes::Kind::PolarDay;
  ASSERT_ENUM_EQ(NightWindow::Kind::Never, nightWindowFor(NightMode::Sun, 0, 0, sun, true).kind);
  sun.kind = SunTimes::Kind::PolarNight;
  w = nightWindowFor(NightMode::Sun, 0, 0, sun, true);
  TEST_ASSERT_TRUE(isNight(w, 12 * 60));
  TEST_ASSERT_EQUAL_INT(1440, nightWindowMinutes(w));

  ASSERT_ENUM_EQ(NightWindow::Kind::Never, nightWindowFor(NightMode::Schedule, 600, 600, sun, true).kind);
}

static void test_night_mode_codes(void) {
  NightMode m = NightMode::Off;
  TEST_ASSERT_TRUE(nightModeFromCode("sun", &m));
  ASSERT_ENUM_EQ(NightMode::Sun, m);
  TEST_ASSERT_TRUE(nightModeFromCode("schedule", &m));
  ASSERT_ENUM_EQ(NightMode::Schedule, m);
  TEST_ASSERT_TRUE(nightModeFromCode("off", &m));
  ASSERT_ENUM_EQ(NightMode::Off, m);
  TEST_ASSERT_FALSE(nightModeFromCode("dark", &m));
  ASSERT_ENUM_EQ(NightMode::Off, m);
  TEST_ASSERT_EQUAL_STRING("schedule", nightModeCode(NightMode::Schedule));
  ASSERT_ENUM_EQ(kDefaultNightMode, clampNightMode(7));
  ASSERT_ENUM_EQ(NightMode::Schedule, clampNightMode(1));
}

static void test_clock_minutes(void) {
  int m = -1;
  TEST_ASSERT_TRUE(parseClockMinutes("23:00", &m));
  TEST_ASSERT_EQUAL_INT(1380, m);
  TEST_ASSERT_TRUE(parseClockMinutes("00:00", &m));
  TEST_ASSERT_EQUAL_INT(0, m);
  TEST_ASSERT_FALSE(parseClockMinutes("24:00", &m));
  TEST_ASSERT_FALSE(parseClockMinutes("6:30", &m));
  TEST_ASSERT_FALSE(parseClockMinutes("06:60", &m));
  TEST_ASSERT_FALSE(parseClockMinutes("06-30", &m));
  TEST_ASSERT_FALSE(parseClockMinutes("", &m));
  TEST_ASSERT_EQUAL_STRING("06:30", formatClockMinutes(390).c_str());
  TEST_ASSERT_EQUAL_STRING("00:05", formatClockMinutes(1445).c_str());
}

// ---- decideBacklight -------------------------------------------------------------

static BacklightInputs dayInputs() {
  BacklightInputs in;
  in.time_synced = true;
  in.now_local_min = 12 * 60;
  in.night.kind = NightWindow::Kind::Window;
  in.night.from_min = 23 * 60;
  in.night.to_min = 6 * 60 + 30;
  in.light_ready = true;
  in.light_avg = 0;  // bright room
  in.now_ms = 100000;
  return in;
}

static BacklightInputs nightInputs() {
  BacklightInputs in = dayInputs();
  in.now_local_min = 1 * 60;
  return in;
}

static void test_day_auto_and_fixed(void) {
  BacklightInputs in = dayInputs();
  BacklightDecision d = decideBacklight(in, 100);
  TEST_ASSERT_EQUAL_UINT8(100, d.target_pct);
  TEST_ASSERT_FALSE(d.night);
  TEST_ASSERT_EQUAL_UINT32(kDayRampMs, d.ramp_ms);

  in.light_avg = 4000;  // dark room
  TEST_ASSERT_EQUAL_UINT8(10, decideBacklight(in, 100).target_pct);

  in.light_ready = false;  // just booted: full until the sensor settles
  TEST_ASSERT_EQUAL_UINT8(100, decideBacklight(in, 100).target_pct);

  in.auto_mode = false;
  in.fixed_pct = 42;
  TEST_ASSERT_EQUAL_UINT8(42, decideBacklight(in, 100).target_pct);
}

static void test_service_screen_is_always_full(void) {
  BacklightInputs in = nightInputs();
  in.night_pct = 0;
  in.service_screen = true;
  const BacklightDecision d = decideBacklight(in, 10);
  TEST_ASSERT_EQUAL_UINT8(100, d.target_pct);
  TEST_ASSERT_FALSE(d.screen_off);
  TEST_ASSERT_EQUAL_UINT32(0, d.ramp_ms);
}

static void test_no_clock_no_night(void) {
  BacklightInputs in = nightInputs();
  in.time_synced = false;
  in.night_pct = 0;
  const BacklightDecision d = decideBacklight(in, 100);
  TEST_ASSERT_FALSE(d.night);
  TEST_ASSERT_EQUAL_UINT8(100, d.target_pct);
}

static void test_night_dims_and_never_brightens(void) {
  BacklightInputs in = nightInputs();
  BacklightDecision d = decideBacklight(in, 100);
  TEST_ASSERT_TRUE(d.night);
  TEST_ASSERT_EQUAL_UINT8(10, d.target_pct);  // default night 10 %
  TEST_ASSERT_FALSE(d.screen_off);
  TEST_ASSERT_EQUAL_UINT32(kNightRampMs, d.ramp_ms);

  in.night_pct = 30;
  in.auto_mode = false;
  in.fixed_pct = 20;  // darker than the night level: stays 20
  TEST_ASSERT_EQUAL_UINT8(20, decideBacklight(in, 100).target_pct);
}

static void test_night_off_and_touch_wake(void) {
  BacklightInputs in = nightInputs();
  in.night_pct = 0;
  BacklightDecision d = decideBacklight(in, 100);
  TEST_ASSERT_TRUE(d.screen_off);
  TEST_ASSERT_EQUAL_UINT8(0, d.target_pct);

  uint32_t until = 0;
  TEST_ASSERT_TRUE(onTouchWake(d, in.now_ms, 60, &until));  // swallowed: it only lit the screen
  in.woken_until_ms = until;
  d = decideBacklight(in, 100);
  TEST_ASSERT_TRUE(d.woken);
  TEST_ASSERT_FALSE(d.screen_off);
  TEST_ASSERT_EQUAL_UINT8(kWakeMinPct, d.target_pct);
  TEST_ASSERT_EQUAL_UINT32(0, d.ramp_ms);  // at once

  // While lit, a touch extends the wake and reaches the UI.
  TEST_ASSERT_FALSE(onTouchWake(d, in.now_ms + 30000, 60, &until));
  in.now_ms += 89000;  // 89 s after the first touch, 59 s after the second
  in.woken_until_ms = until;
  TEST_ASSERT_TRUE(decideBacklight(in, 100).woken);
  in.now_ms += 2000;
  d = decideBacklight(in, 100);
  TEST_ASSERT_FALSE(d.woken);
  TEST_ASSERT_TRUE(d.screen_off);

  // Higher minimum brightness wins over kWakeMinPct.
  in.min_pct = 25;
  in.woken_until_ms = in.now_ms + 1000;
  TEST_ASSERT_EQUAL_UINT8(25, decideBacklight(in, 100).target_pct);
}

static void test_wake_time_is_configurable(void) {
  BacklightInputs in = nightInputs();
  in.night_pct = 0;
  const BacklightDecision d = decideBacklight(in, 100);
  uint32_t until = 0;
  onTouchWake(d, in.now_ms, 20, &until);
  in.woken_until_ms = until;
  in.now_ms += 19000;
  TEST_ASSERT_TRUE(decideBacklight(in, 100).woken);
  in.now_ms += 2000;
  TEST_ASSERT_FALSE(decideBacklight(in, 100).woken);
}

static void test_touch_by_day_does_nothing(void) {
  const BacklightDecision d = decideBacklight(dayInputs(), 100);
  uint32_t until = 0;
  TEST_ASSERT_FALSE(onTouchWake(d, 1000, 60, &until));
  TEST_ASSERT_EQUAL_UINT32(0, until);
}

static void test_wake_survives_millis_wrap(void) {
  BacklightInputs in = nightInputs();
  in.night_pct = 0;
  in.now_ms = 0xFFFFF000u;
  const BacklightDecision d = decideBacklight(in, 100);
  uint32_t until = 0;
  onTouchWake(d, in.now_ms, 60, &until);
  in.woken_until_ms = until;
  in.now_ms += 30000;  // wrapped past 0
  TEST_ASSERT_TRUE(decideBacklight(in, 100).woken);
  in.now_ms += 31000;
  TEST_ASSERT_FALSE(decideBacklight(in, 100).woken);
  TEST_ASSERT_TRUE(msBefore(0xFFFFFFF0u, 0x10u));
  TEST_ASSERT_FALSE(msBefore(0x10u, 0xFFFFFFF0u));
}

static void test_polling_pause(void) {
  BacklightInputs in = nightInputs();
  BacklightDecision d = decideBacklight(in, 100);
  TEST_ASSERT_TRUE(shouldPollOpenSky(d, false));  // default: no pause
  TEST_ASSERT_FALSE(shouldPollOpenSky(d, true));  // night, pause on (screen at 10 %)
  uint32_t until = 0;
  TEST_ASSERT_FALSE(onTouchWake(d, in.now_ms, 60, &until));  // lit screen: a normal tap
  in.woken_until_ms = until;
  d = decideBacklight(in, 100);
  TEST_ASSERT_TRUE(shouldPollOpenSky(d, true));  // woken: data again
  TEST_ASSERT_EQUAL_UINT8(10, d.target_pct);     // brightness unchanged at 10 %
  TEST_ASSERT_TRUE(shouldPollOpenSky(decideBacklight(dayInputs(), 100), true));
}

static void test_status_json(void) {
  BacklightStatus s;
  s.raw = 412;
  s.avg = 397.6f;
  s.pct = 35;
  s.pwm = 61;
  s.synced = true;
  s.sun_known = true;
  s.sun_local.sunrise_min = 7 * 60 + 17;
  s.sun_local.sunset_min = 17 * 60 + 23;
  TEST_ASSERT_EQUAL_STRING(
      "{\"raw\":412,\"avg\":398,\"pct\":35,\"pwm\":61,\"night\":false,\"woken\":false,"
      "\"screen_off\":false,\"polling\":true,\"synced\":true,\"sun\":\"normal\","
      "\"sunrise\":\"07:17\",\"sunset\":\"17:23\"}",
      lightStatusJson(s).c_str());
  s.sun_local.kind = SunTimes::Kind::PolarNight;
  s.polling = false;
  const std::string j = lightStatusJson(s);
  ASSERT_CONTAINS(j, "\"sun\":\"polar_night\",\"sunrise\":\"\",\"sunset\":\"\"");
  ASSERT_CONTAINS(j, "\"polling\":false");
}

}  // namespace

void run_backlight_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_filter_drops_single_spikes);
  RUN_TEST(test_filter_needs_warmup_then_follows);
  RUN_TEST(test_filter_keeps_ready_after_many_samples);
  RUN_TEST(test_auto_curve_ends);
  RUN_TEST(test_auto_curve_middle_and_swapped_limits);
  RUN_TEST(test_auto_curve_hysteresis);
  RUN_TEST(test_pct_to_pwm);
  RUN_TEST(test_step_pwm);
  RUN_TEST(test_window_through_midnight);
  RUN_TEST(test_window_within_a_day_and_empty);
  RUN_TEST(test_window_for_each_mode);
  RUN_TEST(test_night_mode_codes);
  RUN_TEST(test_clock_minutes);
  RUN_TEST(test_day_auto_and_fixed);
  RUN_TEST(test_service_screen_is_always_full);
  RUN_TEST(test_no_clock_no_night);
  RUN_TEST(test_night_dims_and_never_brightens);
  RUN_TEST(test_night_off_and_touch_wake);
  RUN_TEST(test_wake_time_is_configurable);
  RUN_TEST(test_touch_by_day_does_nothing);
  RUN_TEST(test_wake_survives_millis_wrap);
  RUN_TEST(test_polling_pause);
  RUN_TEST(test_status_json);
}
