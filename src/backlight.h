// backlight — automatic brightness from the CYD's light sensor (LDR on
// GPIO34) and the night mode (by the clock, or sunset to sunrise), driving
// the PWM backlight on GPIO21. See spec/03-brightness-night-mode.md.
//
// Split per CLAUDE.md "Testing": the sensor filter, the light -> brightness
// curve, the night decision, touch wake-up and "may we poll OpenSky" are
// pure and run under `pio test -e native`; backlightBegin() / backlightLoop()
// (ADC + tft.setBrightness) are the thin device adapter.
#pragma once

#include <cstdint>
#include <string>

#include "sun_times.h"

// ---- Sensor (measured on the board, spec/03 §10) -------------------------
// Read at 11 dB attenuation: a lit room reads ~0, darkness ~1100-1300
// (higher = darker). The backlight itself shines into the sensor in the dark
// (PWM 255 -> ~260, PWM 10 -> ~1100 under a cloth) — that feedback pushes the
// right way (darker -> dimmer -> reads darker), so it isn't compensated.
constexpr uint16_t kLightRawBright = 40;   // at or below: brightness_max
constexpr uint16_t kLightRawDark = 1000;   // at or above: brightness_min
constexpr uint32_t kLightSampleMs = 100;
constexpr int kLightWarmupSamples = 20;    // ~2 s on full brightness after boot
constexpr uint8_t kLightHysteresisPct = 5;
constexpr uint8_t kMinVisiblePwm = 8;      // any pct > 0 gets at least this duty
constexpr uint8_t kWakeMinPct = 10;        // a touch at night lights at least this much
constexpr uint32_t kDayRampMs = 1000;      // full range, changes during the day
constexpr uint32_t kNightRampMs = 3000;    // full range, into / out of the night

// Median of the last 5 samples (drops single ADC spikes), then an EMA
// (alpha 0.1 -> ~1 s at kLightSampleMs).
class LightFilter {
 public:
  void add(uint16_t raw);
  bool ready() const { return count_ >= kLightWarmupSamples; }
  float average() const { return avg_; }
  uint16_t last() const { return last_; }

 private:
  uint16_t window_[5] = {0, 0, 0, 0, 0};
  int count_ = 0;
  float avg_ = 0.0f;
  uint16_t last_ = 0;
};

// Light reading -> brightness in percent, min..max (linear between
// kLightRawBright and kLightRawDark). Hysteresis: keeps `previousPct` unless
// the new value differs by >= kLightHysteresisPct, or previousPct is outside
// min..max (settings changed).
uint8_t autoBrightnessPct(float avgRaw, uint8_t minPct, uint8_t maxPct, uint8_t previousPct);

// Percent -> PWM duty: 0 -> 0, otherwise kMinVisiblePwm + (255 - min) * (pct/100)^2
// (perceived brightness isn't linear in duty), 100 -> 255.
uint8_t pctToPwm(uint8_t pct);

// One step from `current` toward `target`, at most 255 * dtMs / fullRangeMs
// (at least 1); fullRangeMs 0 = jump.
uint8_t stepPwm(uint8_t current, uint8_t target, uint32_t dtMs, uint32_t fullRangeMs);

// ---- Night ----------------------------------------------------------------

enum class NightMode : uint8_t { Off = 0, Schedule = 1, Sun = 2 };
constexpr NightMode kDefaultNightMode = NightMode::Sun;
NightMode clampNightMode(int raw);
const char *nightModeCode(NightMode m);  // "off" / "schedule" / "sun"
// "off" / "schedule" / "sun" -> mode; false for anything else.
bool nightModeFromCode(const std::string &code, NightMode *out);

// Window in minutes after local midnight; through midnight (23:00-06:30) or
// not (01:00-05:00); from == to -> never.
bool inNightWindow(int nowMin, int fromMin, int toMin);

struct NightWindow {
  enum class Kind { Never, Always, Window } kind = Kind::Never;
  int from_min = 0;
  int to_min = 0;
};

// Off -> Never; Schedule -> from..to; Sun -> sunset..sunrise (local times),
// polar day Never, polar night Always, no home location / unknown sun Never.
NightWindow nightWindowFor(NightMode mode, int fromMin, int toMin, const SunTimes &sunLocal,
                           bool sunKnown);
bool isNight(const NightWindow &w, int nowMin);
int nightWindowMinutes(const NightWindow &w);  // per day: 0 / 1440 / its length

// "HH:MM" <-> minutes (00:00..23:59). parseClockMinutes() is strict.
bool parseClockMinutes(const std::string &s, int *minutes);
std::string formatClockMinutes(int minutes);

// ---- Decision ---------------------------------------------------------------

struct BacklightInputs {
  bool time_synced = false;   // no clock -> no night
  int now_local_min = 0;      // 0..1439
  NightWindow night;
  bool auto_mode = true;
  uint8_t fixed_pct = 80;
  uint8_t min_pct = 10;
  uint8_t max_pct = 100;
  uint8_t night_pct = 10;
  bool light_ready = false;
  float light_avg = 0.0f;
  uint32_t now_ms = 0;
  uint32_t woken_until_ms = 0;  // touch at night (onTouchWake()); 0 = never woken
  bool service_screen = false;  // OTA / password reset / update question
};

struct BacklightDecision {
  uint8_t target_pct = 100;
  uint8_t day_pct = 100;     // what the day rules give (kept for the hysteresis)
  bool night = false;        // inside the night window
  bool woken = false;        // night, but within the wake time after a touch
  bool screen_off = false;   // target 0: the backlight is off
  uint32_t ramp_ms = kDayRampMs;
};

// `previousDayPct`: the last decision's day_pct (hysteresis of the auto curve).
BacklightDecision decideBacklight(const BacklightInputs &in, uint8_t previousDayPct);

// True while `nowMs` is before `untilMs` (millis() wrap-safe, within ~24 days).
bool msBefore(uint32_t nowMs, uint32_t untilMs);

// A touch: at night, (re)starts the wake time (wakeS seconds from now). True
// = the touch only lit a dark screen and must not reach the UI.
bool onTouchWake(const BacklightDecision &d, uint32_t nowMs, uint32_t wakeS,
                 uint32_t *wokenUntilMs);

// OpenSky polling: yes, unless it's night, pausing is on and nobody touched
// the screen within the wake time.
bool shouldPollOpenSky(const BacklightDecision &d, bool pauseAtNight);

// What the config page shows / GET /light.json returns.
struct BacklightStatus {
  uint16_t raw = 0;
  float avg = 0.0f;
  uint8_t pct = 100;
  uint8_t pwm = 255;
  bool night = false;
  bool woken = false;
  bool screen_off = false;
  bool polling = true;
  bool synced = false;
  bool sun_known = false;  // home location set and clock synced
  SunTimes sun_local;      // today's, local time
};

// {"raw":412,"avg":398,"pct":35,"pwm":61,"night":false,...,"sunrise":"07:17","sunset":"17:23"}
// ("sunrise"/"sunset" "" when unknown or polar; "sun":"normal"/"polar_day"/"polar_night"/"").
std::string lightStatusJson(const BacklightStatus &s);

#ifdef ARDUINO

class LGFX;
struct Config;

// After tft.init(): ADC attenuation, backlight to full.
void backlightBegin(LGFX &tft);
// Every loop(): samples the sensor, decides, ramps the PWM.
void backlightLoop(LGFX &tft, const Config &cfg, bool serviceScreen);
// A service screen (OTA progress...) is about to be drawn from outside
// loop(): full brightness now.
void backlightFullNow(LGFX &tft);
const BacklightDecision &backlightDecision();
// A new touch: true = swallowed (it only woke a dark screen).
bool backlightHandleTouch(const Config &cfg);
// Safe from either core: a copy taken at the end of the last backlightLoop().
BacklightStatus backlightStatus(const Config &cfg);

#endif  // ARDUINO
