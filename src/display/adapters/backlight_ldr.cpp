#include "display/adapters/backlight_ldr.h"

#include <Arduino.h>

#include <algorithm>
#include <ctime>

#include "platform/core_link.h"  // Guarded<T>
#include "platform/time_sync.h"

namespace {

// backlightLoop() runs on the UI core; the config page (GET /light.json, the
// General tab) reads the status on the network core — through this copy,
// refreshed every pass (a few dozen bytes under a spinlock).
struct SharedLight {
  BacklightStatus status;
  BacklightDecision decision;
};
Guarded<SharedLight> shared;

constexpr int kLightPin = 34;

LightFilter filter;
BacklightDecision decision;
uint8_t pwmNow = 255;
uint8_t loggedPct = 255;      // last level written to the log
bool loggedNight = false;
uint32_t lastSampleMs = 0;
uint32_t lastStepMs = 0;
uint32_t wokenUntilMs = 0;
uint32_t rampMs = kDayRampMs;  // kNightRampMs while crossing into / out of the night
bool lastNight = false;

// Today's sun (refreshSunToday(): recomputed when the local date, the UTC
// offset or the home location changes).
SunToday sunToday;

void refreshSun(const Home &home) {
  if (!refreshSunToday(sunToday, isTimeSynced(), time(nullptr), timeSyncNowLocal(), home)) {
    return;
  }
  if (sunToday.local.kind == SunTimes::Kind::Normal) {
    Serial.printf("[light] sun today: sunrise %s, sunset %s\n",
                  formatClockMinutes(sunToday.local.sunrise_min).c_str(),
                  formatClockMinutes(sunToday.local.sunset_min).c_str());
  }
}

int nowLocalMinutes() {
  const std::time_t local = timeSyncNowLocal();
  std::tm t;
  gmtime_r(&local, &t);
  return t.tm_hour * 60 + t.tm_min;
}

NightWindow windowFor(const BacklightSettings &settings) {
  return nightWindowFor(settings.night_mode, settings.night_from_min, settings.night_to_min,
                        sunToday.local, sunToday.known);
}

}  // namespace

void backlightBegin(LGFX &tft) {
  analogSetPinAttenuation(kLightPin, ADC_11db);
  pinMode(kLightPin, INPUT);
  pwmNow = 255;
  tft.setBrightness(pwmNow);  // LovyanGFX starts at 127
}

void backlightFullNow(LGFX &tft) {
  pwmNow = 255;
  tft.setBrightness(pwmNow);
}

void backlightLoop(LGFX &tft, const BacklightSettings &settings, const Home &home,
                   bool serviceScreen) {
  const uint32_t now = millis();
  if (now - lastSampleMs >= kLightSampleMs) {
    lastSampleMs = now;
    filter.add(static_cast<uint16_t>(analogRead(kLightPin)));
  }
  refreshSun(home);

  BacklightInputs in;
  in.time_synced = isTimeSynced();
  in.now_local_min = in.time_synced ? nowLocalMinutes() : 0;
  in.night = windowFor(settings);
  in.auto_mode = settings.brightness_auto;
  in.fixed_pct = settings.brightness_fixed_pct;
  in.min_pct = settings.brightness_min_pct;
  in.max_pct = settings.brightness_max_pct;
  in.night_pct = settings.night_pct;
  in.light_ready = filter.ready();
  in.light_avg = filter.average();
  in.now_ms = now;
  in.woken_until_ms = wokenUntilMs;
  in.service_screen = serviceScreen;
  decision = decideBacklight(in, decision.day_pct);

  if (decision.night != lastNight) {
    lastNight = decision.night;
    rampMs = kNightRampMs;
  }
  const uint8_t target = pctToPwm(decision.target_pct);
  const uint32_t ramp = decision.ramp_ms == 0 ? 0 : std::max(decision.ramp_ms, rampMs);
  const uint8_t next = stepPwm(pwmNow, target, now - lastStepMs, ramp);
  lastStepMs = now;
  if (next != pwmNow) {
    pwmNow = next;
    tft.setBrightness(pwmNow);
  }
  if (pwmNow == target) rampMs = kDayRampMs;

  if (decision.night != loggedNight) {
    loggedNight = decision.night;
    Serial.printf("[light] %s (%s): %u%%%s\n", decision.night ? "night" : "day",
                  nightModeCode(settings.night_mode), static_cast<unsigned>(decision.target_pct),
                  decision.screen_off ? ", backlight off" : "");
  }
  SharedLight snap;
  snap.status.raw = filter.last();
  snap.status.avg = filter.average();
  snap.status.pct = decision.target_pct;
  snap.status.pwm = pwmNow;
  snap.status.night = decision.night;
  snap.status.woken = decision.woken;
  snap.status.screen_off = decision.screen_off;
  snap.status.synced = in.time_synced;
  snap.status.sun_known = sunToday.known;
  snap.status.sun_local = sunToday.local;
  snap.decision = decision;
  shared.set(snap);

  if (!serviceScreen && decision.target_pct != loggedPct) {
    loggedPct = decision.target_pct;
    Serial.printf("[light] raw=%u avg=%.0f -> %u%% (pwm %u)\n", filter.last(), filter.average(),
                  static_cast<unsigned>(decision.target_pct),
                  static_cast<unsigned>(pctToPwm(decision.target_pct)));
  }
}

const BacklightDecision &backlightDecision() { return decision; }

bool backlightHandleTouch(const BacklightSettings &settings) {
  const bool swallowed = onTouchWake(decision, millis(), settings.night_wake_s, &wokenUntilMs);
  if (decision.night) {
    Serial.printf("[light] touch at night: awake for %u s%s\n",
                  static_cast<unsigned>(settings.night_wake_s), swallowed ? " (screen lit)" : "");
  }
  return swallowed;
}

BacklightStatus backlightStatus(const BacklightSettings &settings) {
  const SharedLight snap = shared.get();
  BacklightStatus s = snap.status;
  s.polling = shouldPollOpenSky(snap.decision, settings.night_pause_polling);
  return s;
}
