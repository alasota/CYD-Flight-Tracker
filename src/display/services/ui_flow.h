// ui_flow — the UI loop's own bookkeeping on core 1, apart from drawing and
// touch reading: what a tap does (routeTap()), the table page (TablePager),
// what a screen change sets off (ScreenChangeTracker: persist, page, the
// auto-cycle timer), the header clock's minute (MinuteWatch), the NOW
// countdown's 1 Hz tick (CountdownTick), whether auto-cycle switches now
// (shouldAutoCycleNow()), the health the header shows (shownHealthStep()),
// touch edges (TouchEdge) and what the normal screens are drawn from
// (UiView, read out of net_task's NetState). See docs/screens.md "Screen
// navigation" and docs/screens.md "Auto screen cycling".
//
// Pure — tested under `pio test -e native`; main.cpp reads the touch, calls
// these and acts on the answers.
#pragma once

#include <cstdint>
#include <ctime>
#include <vector>

#include "airspace/core/feed_health.h"     // FeedHealth
#include "airspace/core/tracked_flight.h"  // TrackedFlight
#include "display/core/i18n.h"             // Language
#include "display/core/redraw_policy.h"    // RedrawEvents
#include "display/core/screen_nav.h"       // kScreen*, ScreenChangeCause, alertBadgeHit()
#include "display/core/screens/status_bar.h"  // HeaderZones
#include "display/services/service_screen_flows.h"  // ServiceOutcome
#include "platform/core_link.h"               // NetState, UiWake

// ---- What the screens show -------------------------------------------------------

// The rows of net_task's last good poll; empty before the first one.
const std::vector<TrackedFlight> &netRows(const NetState &s);

// The NOW aircraft's CPA extrapolated from its poll to `nowMs` (cpa_predictor
// owns the math — docs/review-notes-2026.md 3.4); not found before the first poll.
CpaPrediction netCpaAt(const NetState &s, uint32_t nowMs);

// Everything the normal screens are drawn from in one pass
// (display/adapters/lgfx/ui_draw). `rows` points into the NetState.
struct UiView {
  int screen = kScreenFlights;
  int page = 0;
  const std::vector<TrackedFlight> *rows = nullptr;
  bool hasData = false;   // a poll has come in
  int nowRowIndex = -1;   // the NOW aircraft in rows, -1 = none
  CpaPrediction cpa;      // netCpaAt()
  float radiusDeg = 0.0f;
  FeedHealth health = FeedHealth::Ok;  // the one the header shows
  AlertSeverity alertBadge = AlertSeverity::None;
  std::time_t local = 0;  // timeSyncNowLocal()
  bool synced = false;
  bool homeConfigured = false;
};

UiView makeUiView(const NetState &s, int screen, int page, FeedHealth shownHealth,
                  uint32_t nowMs, std::time_t local, bool synced);

// ---- Taps ---------------------------------------------------------------------

enum class TapKind : uint8_t {
  None,        // nothing to do
  OpenAlerts,  // the header's "!" — the alert message screen
  JumpTo,      // a bottom nav tab — TapAction::target
  PagePrev,    // the table's first-row strip, not on the first page
  PageNext,    // the table's last-row strip, not on the last page
};

struct TapAction {
  TapKind kind = TapKind::None;
  int target = -1;  // screen index, only for JumpTo
};

struct TapContext {
  int currentScreen = kScreenFlights;
  int page = 0;       // the table page shown
  int pageCount = 0;  // tablePageCount() of the rows shown
  bool homeConfigured = false;
  HeaderZones header;  // the header as drawn (alert_x < 0: no "!")
  int16_t screenW = 320;
  int16_t screenH = 240;
};

// One tap: the "!" first (only with home set and a badge drawn), then the
// bottom nav, then — on the Flights screen only — the table's paging strips
// (first row from tableFirstRowY(), last row just above the nav, both
// tableRowHeightPx() tall), never past the first or the last page.
TapAction routeTap(int16_t x, int16_t y, const TapContext &c);

// The context of a tap on the screens drawn from `v`: its screen and page, the
// pages of its rows, the header with the "!" as drawStatusBar() draws it.
TapContext tapContextOf(const UiView &v, int16_t screenW, int16_t screenH, Language lang);

// Pages of the table below the featured row (splitFeaturedAndRest()).
int tablePageCount(const std::vector<TrackedFlight> &rows);

// The Flights table's page.
class TablePager {
 public:
  int page() const;
  void reset();                // back to the first page
  void clamp(int pageCount);   // a shorter list: the last page that exists (0 when empty)
  bool prev();                 // true = the page changed
  bool next(int pageCount);    // true = the page changed

 private:
  int page_ = 0;
};

// ---- Screen changes -------------------------------------------------------------

struct ScreenChangeResult {
  bool persist = false;  // saveLastScreen(screen) now
};

// What a change of the active screen sets off: the table back to page 0, the
// auto-cycle timer from now, screenChanged for the redraw, and whether to
// write last_screen to NVS (shouldPersistScreenChange(); never when the
// screen is the one already saved).
class ScreenChangeTracker {
 public:
  // Boot: `screen` is the one saved, both timers start at `nowMs`.
  void begin(int screen, uint32_t nowMs);
  ScreenChangeResult onChange(int screen, ScreenChangeCause cause, uint32_t nowMs,
                              TablePager &pager, RedrawEvents &pending);
  // A fresh auto-cycle interval without a screen change (alert screen closed,
  // a touch that only woke a dark screen).
  void restartAutoCycle(uint32_t nowMs);
  // Ms since the last screen change (or restart) — shouldAutoAdvance()'s elapsedMs.
  uint32_t autoCycleElapsedMs(uint32_t nowMs) const;

 private:
  int persistedScreen_ = 0;
  uint32_t persistedAtMs_ = 0;
  uint32_t changedAtMs_ = 0;
};

// ---- Header clock, NOW countdown --------------------------------------------------

// The header's HH:MM: changed() is true on the first pass, at the first NTP
// sync and when the minute rolls over — not more often.
class MinuteWatch {
 public:
  // `minuteNow`: minuteOf(local time) when synced, -1 when not.
  bool changed(long minuteNow);

 private:
  long last_ = -2;
};

// The NOW screen's countdown: due() once a second, and only on kScreenFlight.
class CountdownTick {
 public:
  bool due(int currentScreen, uint32_t nowMs);
  // Ms until the next due() on the NOW screen (UiWake::untilCountdownMs).
  uint32_t msUntilNext(uint32_t nowMs) const;

 private:
  uint32_t lastMs_ = 0;
};

// ---- Auto-cycle ---------------------------------------------------------------

struct AutoCycleInput {
  bool enabled = false;         // auto_cycle.enabled
  bool homeConfigured = false;  // the setup prompt has the screen otherwise
  bool screenOff = false;       // night, backlight off — nobody sees it change
  int currentScreen = kScreenFlights;
  bool cpaFound = false;        // for shouldDeferAutoSwitch()
  float tCpaSeconds = 0.0f;
  uint32_t elapsedMs = 0;       // ScreenChangeTracker::autoCycleElapsedMs()
  uint32_t intervalS = 15;      // auto_cycle.interval_s
};

// enabled && homeConfigured && !screenOff && shouldAutoAdvance(elapsed,
// interval, shouldDeferAutoSwitch(...)).
bool shouldAutoCycleNow(const AutoCycleInput &in);

// The input from the settings in `s` (auto_cycle_*, home) and the NOW
// aircraft's CPA at `nowMs`.
AutoCycleInput autoCycleInputAt(const NetState &s, int currentScreen, bool screenOff,
                                uint32_t elapsedMs, uint32_t nowMs);

// ---- The health the header shows ------------------------------------------------

struct ShownHealthStep {
  FeedHealth shown = FeedHealth::Ok;
  bool healthChanged = false;  // the header must be redrawn
  bool newPoll = false;        // the empty content says why it is empty
  bool stalled = false;        // net_task silent for kNetStallLimitMs (log it)
};

// net_task's health, or NetworkError once its heartbeat (`netBeatMs`) is older
// than kNetStallLimitMs. A change sets healthChanged, and newPoll too when
// the list is empty.
ShownHealthStep shownHealthStep(FeedHealth shownNow, FeedHealth netHealth, uint32_t nowMs,
                                uint32_t netBeatMs, bool rowsEmpty);

// ---- Touch edges ----------------------------------------------------------------

// One tap per press: tap() is true on the pass where the finger comes down.
class TouchEdge {
 public:
  bool tap(bool pressed);
  // The finger now on the screen is not a tap (it belongs to a screen that
  // just closed) — the next tap needs a new press.
  void swallow();

 private:
  bool wasPressed_ = false;
};

// ---- The UI loop's bookkeeping together ---------------------------------------------

// What main.cpp keeps between passes, apart from the active screen and the
// snapshot: the redraw events since the last redraw, the health the header
// shows, and the pieces above. Its steps are the ones every pass takes.
class UiFlow {
 public:
  RedrawEvents pending;
  FeedHealth shownHealth = FeedHealth::Ok;
  TablePager pager;
  ScreenChangeTracker screenChanges;
  MinuteWatch minuteWatch;
  CountdownTick countdown;
  TouchEdge touch;

  // A new snapshot `next` with diffNetState()'s events: all but its health
  // (the shown one is updateHealth()'s); a new poll clamps the table page.
  void takeNetEvents(const RedrawEvents &e, const NetState &next);
  // shownHealthStep() applied: a change redraws the header (and an empty
  // screen). True = net_task has just gone silent (log it).
  bool updateHealth(const NetState &s, uint32_t nowMs, uint32_t netBeatMs);
  // The active screen changed (ScreenChangeTracker); true = save last_screen now.
  bool screenChanged(int screen, ScreenChangeCause cause, uint32_t nowMs);
  // Carries out a service screen pass's outcome (pending cleared before
  // firstDraw). True = a service screen owns the display.
  bool apply(const ServiceOutcome &o, uint32_t nowMs);
  // A PagePrev / PageNext tap: the page moves within `pageCount`, the table is redrawn.
  void pageTap(TapKind kind, int pageCount);
  // A normal pass's clocks: the NOW countdown second, the header minute
  // (`minuteNow` -1 while unsynced). True = auto-cycle switches now.
  bool tick(const NetState &s, int screen, uint32_t nowMs, long minuteNow, bool screenOff);
  // The regions to repaint now (computeRedrawMask()); nothing pending afterwards.
  uint8_t takeRedrawMask(int screen);
  // The NOW countdown's wake-up while `screen` is the NOW screen.
  void addWakeTimes(UiWake &w, int screen, uint32_t nowMs) const;
};
