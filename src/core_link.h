// core_link — everything that crosses between the two ESP32 cores
// (spec/done/04-dual-core.md §5.1-§5.3).
//
// Core 0 runs the network task (net_task: WiFi, web page, OpenSky, hexdb.io,
// GitHub, OTA); core 1 runs loop() = the UI (display, touch). They share
// nothing but:
//   1. NetState (net -> UI): an immutable snapshot behind a shared_ptr in a
//      "latest wins" LatestBox — publishing swaps one pointer, aircraft rows
//      are never copied (a new NetState shares the previous DataSnapshot).
//   2. NetCommand (UI -> net): small POD commands in a FreeRTOS queue
//      (install / skip a version, refresh).
//   3. A few 32-bit atomics and Guarded<T> copies of small POD structs
//      (heartbeats, polling allowed, UI perf numbers, backlight status).
// A lock is held only to swap a pointer or copy a few bytes — never around
// an allocation, a free, a log line or any I/O.
//
// The types and decisions are pure and native-tested; the globals (box,
// queue, task handles) are behind #ifdef ARDUINO.
#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "config_store.h"   // Config
#include "cpa_predictor.h"  // CpaPrediction
#include "device_alerts.h"  // AlertSet, AlertDetails
#include "redraw_policy.h"  // RedrawEvents
#include "release_check.h"  // UpdateStatus
#include "status_bar.h"     // OpenSkyHealth
#include "table_view.h"     // AircraftRow
#include "wifi_manager.h"   // WifiStatus

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// ---- Lock policy -----------------------------------------------------------

#ifdef ARDUINO
// FreeRTOS spinlock: works across both cores, held for nanoseconds here.
struct SpinLock {
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  void lock() { portENTER_CRITICAL(&mux); }
  void unlock() { portEXIT_CRITICAL(&mux); }
};
#else
// Native tests are single-threaded.
struct SpinLock {
  void lock() {}
  void unlock() {}
};
#endif

// ---- "Latest wins" box: one writer, one reader ----------------------------

template <typename T, typename Lock = SpinLock>
class LatestBox {
 public:
  // Swaps `next` in under the lock; the previous value is released after the
  // lock is dropped (its destructor may free memory — never under a spinlock).
  void publish(std::shared_ptr<const T> next) {
    lock_.lock();
    slot_.swap(next);
    ++seq_;
    lock_.unlock();
    // `next` now holds the old value and is destroyed here, unlocked.
  }

  // The current value if it is newer than `seenSeq` (which is then updated);
  // false = nothing new, `out` untouched. A miss costs one comparison.
  bool takeIfNewer(uint32_t &seenSeq, std::shared_ptr<const T> &out) {
    std::shared_ptr<const T> fresh;
    lock_.lock();
    const uint32_t seq = seq_;
    if (seq != seenSeq) fresh = slot_;  // refcount++ only, no allocation
    lock_.unlock();
    if (seq == seenSeq) return false;
    seenSeq = seq;
    out.swap(fresh);  // the old `out` dies with `fresh`, unlocked
    return true;
  }

  uint32_t seq() {
    lock_.lock();
    const uint32_t s = seq_;
    lock_.unlock();
    return s;
  }

  Lock &lockForTesting() { return lock_; }

 private:
  Lock lock_;
  std::shared_ptr<const T> slot_;
  uint32_t seq_ = 0;
};

// ---- A small POD value copied under a lock --------------------------------

template <typename T, typename Lock = SpinLock>
class Guarded {
  static_assert(std::is_trivially_copyable<T>::value, "Guarded<T> copies T under a spinlock");

 public:
  void set(const T &v) {
    lock_.lock();
    value_ = v;
    lock_.unlock();
  }
  T get() {
    lock_.lock();
    T v = value_;
    lock_.unlock();
    return v;
  }

 private:
  Lock lock_;
  T value_{};
};

// ---- Net -> UI snapshot -----------------------------------------------------

// One poll's processed data (poll_pipeline). Immutable once published.
struct DataSnapshot {
  std::vector<AircraftRow> rows;  // sorted, enriched, phases classified
  int nowRowIndex = -1;           // pickNowAircraft(), -1 = none
  CpaPrediction cpa;              // for nowRowIndex, as of polledAtMs
  uint32_t polledAtMs = 0;        // millis() of the /states/all answer
  uint32_t fetches = 0;           // new hexdb.io requests that went into it
};

// The firmware-update progress screen, drawn by the UI from this (the OTA
// code on core 0 never touches the display).
struct OtaProgress {
  enum class Phase : uint8_t { Idle, Running, Done, Failed };
  Phase phase = Phase::Idle;
  bool fromGithub = false;
  uint32_t written = 0;
  uint32_t total = 0;
  std::string newVersion;  // from the image's marker, "" until seen
  uint32_t seq = 0;        // bumped on every change
};

// What the display's "new version — install it?" question needs.
struct UpdateSummary {
  UpdateStatus status = UpdateStatus::NeverChecked;
  std::string installed;
  std::string latestTag;
  bool installPending = false;  // an install has been asked for / is running
};

struct NetState {
  std::shared_ptr<const DataSnapshot> data;  // nullptr until the first good poll
  std::shared_ptr<const Config> cfg;         // always set
  uint32_t cfgSeq = 0;                       // bumped when the config is reloaded
  OpenSkyHealth health = OpenSkyHealth::Ok;
  WifiStatus wifi = WifiStatus::Connecting;
  bool restartPending = false;  // a good upload: the board restarts in a moment
  UpdateSummary update;
  OtaProgress ota;
  // Device alerts (spec 08): the header's "!" and the message screen.
  AlertSet alerts;
  AlertDetails alertDetails;
};

// True when `b` differs from `a` in anything the UI uses — the network task
// publishes only then (no allocation in a quiet loop). Pointers for data and
// config, values for the rest; OTA progress by its seq. Alert details
// without quotaObservedMs (it moves with every 429 without changing a text).
bool netStateDiffers(const NetState &a, const NetState &b);

// What changed between two snapshots, as redraw events. `prev` == nullptr =
// the first snapshot. Rows are compared by pointer (a new DataSnapshot = a
// new poll); health, language, home location and radius by value; alerts
// by the header badge they show (displaySeverity()).
RedrawEvents diffNetState(const NetState *prev, const NetState &next);

// ---- UI -> net commands ----------------------------------------------------

enum class NetCommandType : uint8_t {
  InstallTag,  // YES on the display's update question
  SkipTag,     // NO: never ask about this version again
  RefreshNow,  // a touch woke the screen at night: fresh data (interval permitting)
};

struct NetCommand {
  NetCommandType type = NetCommandType::RefreshNow;
  char tag[32] = {};  // always NUL-terminated; longer tags are cut
};
static_assert(std::is_trivially_copyable<NetCommand>::value, "goes through a FreeRTOS queue");

NetCommand makeNetCommand(NetCommandType type, const std::string &tag = std::string());
std::string netCommandTag(const NetCommand &c);

// ---- UI loop pacing ---------------------------------------------------------

// Touch is polled this often (LovyanGFX checks the XPT2046's IRQ pin first, so
// a poll without a finger is one GPIO read, no SPI transfer).
constexpr uint32_t kUiTouchPollMs = 20;
// Longest the UI sleeps without a reason to wake.
constexpr uint32_t kUiMaxSleepMs = 1000;
constexpr uint32_t kNoDeadline = UINT32_MAX;

// Ms left until `periodMs` have passed since `lastMs` (0 = due now),
// correct across a millis() rollover.
uint32_t msUntilPeriod(uint32_t nowMs, uint32_t lastMs, uint32_t periodMs);

// Ms until the wall-clock minute rolls over, from the seconds of the minute.
uint32_t msUntilNextMinute(int secondOfMinute, uint32_t msIntoSecond);

struct UiWake {
  uint32_t touchPollMs = kUiTouchPollMs;
  uint32_t untilCountdownMs = kNoDeadline;    // Screen 2's 1 Hz tick
  uint32_t untilMinuteMs = kNoDeadline;       // header clock
  uint32_t untilPromptTimerMs = kNoDeadline;  // update question's countdown bar
  uint32_t untilBacklightMs = kNoDeadline;    // light sensor sample / ramp step
  uint32_t untilAlertTimerMs = kNoDeadline;   // alert screen's CLOSE countdown second
};

// How long loop() may block before its next pass: the nearest deadline,
// capped to [0, kUiMaxSleepMs]. A new NetState wakes it earlier.
uint32_t uiSleepMs(const UiWake &w);

// ---- Liveness ---------------------------------------------------------------

// The network task may legitimately sit ~25 s in one pass (token + states +
// hexdb.io timeouts); no heartbeat for this long means it is stuck.
constexpr uint32_t kNetStallLimitMs = 60000;
// The UI never blocks for long: no heartbeat for this long = it hangs (a new
// firmware image must not be marked valid then).
constexpr uint32_t kUiAliveLimitMs = 2000;

// True when `beatMs` (millis() of the last heartbeat) is older than `limitMs`.
bool heartbeatStale(uint32_t nowMs, uint32_t beatMs, uint32_t limitMs);

// ---- Device-side globals ----------------------------------------------------

#ifdef ARDUINO
#include <atomic>

// UI-side perf numbers handed to the network task (which logs them and
// serves /perf.json) — last complete window plus totals since boot.
struct UiPerfShare {
  uint32_t windowCount = 0, windowMaxMs = 0;
  uint32_t buckets[6] = {};
  uint32_t maxEverMs = 0, slowEver = 0;
  uint32_t stackFree = 0;
};

namespace core_link {

extern LatestBox<NetState> netState;
extern Guarded<UiPerfShare> uiPerf;
extern std::atomic<uint32_t> uiBeatMs;          // UI -> net
extern std::atomic<uint32_t> netBeatMs;         // net -> UI
extern std::atomic<bool> pollingAllowed;        // UI -> net (spec 03 night pause)

// Creates the command queue and remembers the UI task (loop()) for wakeups.
void begin(TaskHandle_t uiTask);
// Wakes the UI task from its sleep (after a publish). Any core, not an ISR.
void wakeUi();
// UI -> net, never blocks; false = queue full (the command is dropped).
bool sendCommand(const NetCommand &c);
// Net side: the next queued command, without waiting.
bool receiveCommand(NetCommand *out);

}  // namespace core_link
#endif  // ARDUINO
