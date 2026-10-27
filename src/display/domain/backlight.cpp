#include "display/domain/backlight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---- Sensor -----------------------------------------------------------------

void LightFilter::add(uint16_t raw) {
  last_ = raw;
  window_[count_ % 5] = raw;
  ++count_;
  if (count_ > 1000) count_ = kLightWarmupSamples + 5;  // stays "ready", never overflows
  const int n = std::min(count_, 5);
  uint16_t sorted[5];
  std::copy(window_, window_ + n, sorted);
  std::sort(sorted, sorted + n);
  const float median = sorted[n / 2];
  avg_ = (count_ == 1) ? median : avg_ + 0.1f * (median - avg_);
}

uint8_t autoBrightnessPct(float avgRaw, uint8_t minPct, uint8_t maxPct, uint8_t previousPct) {
  if (minPct > maxPct) std::swap(minPct, maxPct);
  float t = (avgRaw - kLightRawBright) / static_cast<float>(kLightRawDark - kLightRawBright);
  t = std::min(std::max(t, 0.0f), 1.0f);  // 0 = bright room, 1 = dark
  const int pct = static_cast<int>(std::lround(maxPct - t * (maxPct - minPct)));
  if (previousPct < minPct || previousPct > maxPct) return static_cast<uint8_t>(pct);
  if (std::abs(pct - previousPct) < kLightHysteresisPct && pct != minPct && pct != maxPct) {
    return previousPct;
  }
  return static_cast<uint8_t>(pct);
}

uint8_t pctToPwm(uint8_t pct) {
  if (pct == 0) return 0;
  if (pct >= 100) return 255;
  const float f = pct / 100.0f;
  const int pwm = kMinVisiblePwm + static_cast<int>(std::lround((255 - kMinVisiblePwm) * f * f));
  return static_cast<uint8_t>(std::min(pwm, 255));
}

uint8_t stepPwm(uint8_t current, uint8_t target, uint32_t dtMs, uint32_t fullRangeMs) {
  if (fullRangeMs == 0 || current == target) return target;
  uint32_t maxDelta = 255u * dtMs / fullRangeMs;
  if (maxDelta == 0) maxDelta = 1;
  if (target > current) {
    return static_cast<uint8_t>(std::min<uint32_t>(target, current + maxDelta));
  }
  return static_cast<uint8_t>(current - std::min<uint32_t>(current - target, maxDelta));
}

// ---- Night ------------------------------------------------------------------

NightMode clampNightMode(int raw) {
  if (raw < 0 || raw > static_cast<int>(NightMode::Sun)) return kDefaultNightMode;
  return static_cast<NightMode>(raw);
}

const char *nightModeCode(NightMode m) {
  switch (m) {
    case NightMode::Off:
      return "off";
    case NightMode::Schedule:
      return "schedule";
    case NightMode::Sun:
    default:
      return "sun";
  }
}

bool nightModeFromCode(const std::string &code, NightMode *out) {
  for (NightMode m : {NightMode::Off, NightMode::Schedule, NightMode::Sun}) {
    if (code == nightModeCode(m)) {
      *out = m;
      return true;
    }
  }
  return false;
}

bool inNightWindow(int nowMin, int fromMin, int toMin) {
  if (fromMin == toMin) return false;
  if (fromMin < toMin) return nowMin >= fromMin && nowMin < toMin;
  return nowMin >= fromMin || nowMin < toMin;  // through midnight
}

NightWindow nightWindowFor(NightMode mode, int fromMin, int toMin, const SunTimes &sunLocal,
                           bool sunKnown) {
  NightWindow w;
  switch (mode) {
    case NightMode::Schedule:
      w.kind = fromMin == toMin ? NightWindow::Kind::Never : NightWindow::Kind::Window;
      w.from_min = fromMin;
      w.to_min = toMin;
      break;
    case NightMode::Sun:
      if (!sunKnown || sunLocal.kind == SunTimes::Kind::PolarDay) break;
      if (sunLocal.kind == SunTimes::Kind::PolarNight) {
        w.kind = NightWindow::Kind::Always;
        break;
      }
      w.kind = NightWindow::Kind::Window;
      w.from_min = sunLocal.sunset_min;
      w.to_min = sunLocal.sunrise_min;
      break;
    case NightMode::Off:
    default:
      break;
  }
  return w;
}

bool isNight(const NightWindow &w, int nowMin) {
  switch (w.kind) {
    case NightWindow::Kind::Always:
      return true;
    case NightWindow::Kind::Window:
      return inNightWindow(nowMin, w.from_min, w.to_min);
    case NightWindow::Kind::Never:
    default:
      return false;
  }
}

int nightWindowMinutes(const NightWindow &w) {
  switch (w.kind) {
    case NightWindow::Kind::Always:
      return 1440;
    case NightWindow::Kind::Window:
      if (w.from_min == w.to_min) return 0;
      return w.from_min < w.to_min ? w.to_min - w.from_min : 1440 - w.from_min + w.to_min;
    case NightWindow::Kind::Never:
    default:
      return 0;
  }
}

bool parseClockMinutes(const std::string &s, int *minutes) {
  if (s.size() != 5 || s[2] != ':') return false;
  for (int i : {0, 1, 3, 4}) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  const int h = (s[0] - '0') * 10 + (s[1] - '0');
  const int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return false;
  *minutes = h * 60 + m;
  return true;
}

std::string formatClockMinutes(int minutes) {
  minutes = ((minutes % 1440) + 1440) % 1440;
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

// ---- Decision ---------------------------------------------------------------

bool msBefore(uint32_t nowMs, uint32_t untilMs) {
  return static_cast<int32_t>(untilMs - nowMs) > 0;
}

BacklightDecision decideBacklight(const BacklightInputs &in, uint8_t previousDayPct) {
  BacklightDecision d;
  if (in.service_screen) {
    d.target_pct = 100;
    d.day_pct = previousDayPct;
    d.ramp_ms = 0;
    return d;
  }

  if (in.auto_mode) {
    d.day_pct = in.light_ready
                    ? autoBrightnessPct(in.light_avg, in.min_pct, in.max_pct, previousDayPct)
                    : std::max(in.min_pct, in.max_pct);
  } else {
    d.day_pct = in.fixed_pct;
  }

  d.night = in.time_synced && isNight(in.night, in.now_local_min);
  if (!d.night) {
    d.target_pct = d.day_pct;
    d.ramp_ms = kDayRampMs;
    return d;
  }

  d.woken = in.woken_until_ms != 0 && msBefore(in.now_ms, in.woken_until_ms);
  d.ramp_ms = kNightRampMs;
  if (in.night_pct == 0) {
    if (d.woken) {
      d.target_pct = std::max(in.min_pct, kWakeMinPct);
      d.ramp_ms = 0;  // someone just touched it
    } else {
      d.target_pct = 0;
      d.screen_off = true;
    }
  } else {
    d.target_pct = std::min(in.night_pct, d.day_pct);  // night never brightens the screen
  }
  return d;
}

bool onTouchWake(const BacklightDecision &d, uint32_t nowMs, uint32_t wakeS,
                 uint32_t *wokenUntilMs) {
  if (!d.night) return false;
  *wokenUntilMs = (nowMs + wakeS * 1000u) | 1u;  // 0 means "never woken"
  return d.screen_off;
}

bool shouldPollOpenSky(const BacklightDecision &d, bool pauseAtNight) {
  return !(d.night && pauseAtNight && !d.woken);
}

std::string lightStatusJson(const BacklightStatus &s) {
  const bool normalSun = s.sun_known && s.sun_local.kind == SunTimes::Kind::Normal;
  const char *sun = !s.sun_known                                           ? ""
                    : s.sun_local.kind == SunTimes::Kind::PolarDay   ? "polar_day"
                    : s.sun_local.kind == SunTimes::Kind::PolarNight ? "polar_night"
                                                                     : "normal";
  const std::string rise = normalSun ? formatClockMinutes(s.sun_local.sunrise_min) : "";
  const std::string set = normalSun ? formatClockMinutes(s.sun_local.sunset_min) : "";
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "{\"raw\":%u,\"avg\":%u,\"pct\":%u,\"pwm\":%u,\"night\":%s,\"woken\":%s,"
                "\"screen_off\":%s,\"polling\":%s,\"synced\":%s,\"sun\":\"%s\","
                "\"sunrise\":\"%s\",\"sunset\":\"%s\"}",
                static_cast<unsigned>(s.raw), static_cast<unsigned>(std::lround(s.avg)),
                static_cast<unsigned>(s.pct), static_cast<unsigned>(s.pwm),
                s.night ? "true" : "false", s.woken ? "true" : "false",
                s.screen_off ? "true" : "false", s.polling ? "true" : "false",
                s.synced ? "true" : "false", sun, rise.c_str(), set.c_str());
  return buf;
}

// ---- Device adapter -----------------------------------------------------------

#ifdef ARDUINO

#include <Arduino.h>

#include <ctime>

#include "LGFX_CYD.hpp"
#include "admin/domain/config_store.h"
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

// Today's sun, recomputed when the local date or the home location changes.
SunTimes sunLocal;
bool sunKnown = false;
int sunDayKey = -1;
float sunLat = 0.0f, sunLon = 0.0f;

void refreshSun(const Config &cfg) {
  if (!isTimeSynced() || !cfg.home_configured) {
    sunKnown = false;
    return;
  }
  const std::time_t utc = time(nullptr);
  const std::time_t local = timeSyncNowLocal();
  std::tm t;
  gmtime_r(&local, &t);  // local wall-clock fields (timeSyncNowLocal() is pre-shifted)
  const int dayKey = (t.tm_year + 1900) * 400 + t.tm_yday;
  if (sunKnown && dayKey == sunDayKey && cfg.home_lat == sunLat && cfg.home_lon == sunLon) return;
  const int offsetMin = static_cast<int>((local - utc) / 60);
  sunLocal = shiftSunTimes(
      computeSunTimes(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, cfg.home_lat, cfg.home_lon),
      offsetMin);
  sunKnown = true;
  sunDayKey = dayKey;
  sunLat = cfg.home_lat;
  sunLon = cfg.home_lon;
  if (sunLocal.kind == SunTimes::Kind::Normal) {
    Serial.printf("[light] sun today: sunrise %s, sunset %s\n",
                  formatClockMinutes(sunLocal.sunrise_min).c_str(),
                  formatClockMinutes(sunLocal.sunset_min).c_str());
  }
}

int nowLocalMinutes() {
  const std::time_t local = timeSyncNowLocal();
  std::tm t;
  gmtime_r(&local, &t);
  return t.tm_hour * 60 + t.tm_min;
}

NightWindow windowFor(const Config &cfg) {
  return nightWindowFor(cfg.night_mode, cfg.night_from_min, cfg.night_to_min, sunLocal, sunKnown);
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

void backlightLoop(LGFX &tft, const Config &cfg, bool serviceScreen) {
  const uint32_t now = millis();
  if (now - lastSampleMs >= kLightSampleMs) {
    lastSampleMs = now;
    filter.add(static_cast<uint16_t>(analogRead(kLightPin)));
  }
  refreshSun(cfg);

  BacklightInputs in;
  in.time_synced = isTimeSynced();
  in.now_local_min = in.time_synced ? nowLocalMinutes() : 0;
  in.night = windowFor(cfg);
  in.auto_mode = cfg.brightness_auto;
  in.fixed_pct = cfg.brightness_fixed_pct;
  in.min_pct = cfg.brightness_min_pct;
  in.max_pct = cfg.brightness_max_pct;
  in.night_pct = cfg.night_pct;
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
                  nightModeCode(cfg.night_mode), static_cast<unsigned>(decision.target_pct),
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
  snap.status.sun_known = sunKnown;
  snap.status.sun_local = sunLocal;
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

bool backlightHandleTouch(const Config &cfg) {
  const bool swallowed = onTouchWake(decision, millis(), cfg.night_wake_s, &wokenUntilMs);
  if (decision.night) {
    Serial.printf("[light] touch at night: awake for %u s%s\n",
                  static_cast<unsigned>(cfg.night_wake_s), swallowed ? " (screen lit)" : "");
  }
  return swallowed;
}

BacklightStatus backlightStatus(const Config &cfg) {
  const SharedLight snap = shared.get();
  BacklightStatus s = snap.status;
  s.polling = shouldPollOpenSky(snap.decision, cfg.night_pause_polling);
  return s;
}

#endif  // ARDUINO
