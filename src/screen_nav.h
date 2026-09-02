// screen_nav — which of the three screens (Flights / Flight / Radar) is
// active, cycling with wraparound, plus touch hit-testing for the
// left/right edge strips and the bottom nav bar segments. See CLAUDE.md
// "Screen navigation" and "Visualization concept".
//
// Zero TFT drawing, zero OpenSky/hexdb.io calls — pure index arithmetic
// and rectangle geometry, reusing touch_input's Rect/hitTest. Tested
// under `pio test -e native`.
#pragma once

#include <cstdint>

#include "touch_input.h"  // Rect, hitTest

// Screen order (also the persisted `last_screen` value in config_store):
// Flights -> Flight -> Radar -> wraps back to Flights.
constexpr int kScreenCount = 3;
constexpr int kScreenFlights = 0;
constexpr int kScreenFlight = 1;
constexpr int kScreenRadar = 2;

// Coerce any integer to a valid screen index [0, kScreenCount) with
// wraparound (handles negatives too). A stale/garbage persisted value
// lands on Flights-ish rather than out of bounds.
int wrapScreen(int index);

// Next / previous screen with wraparound: nextScreen(kScreenRadar) ==
// kScreenFlights, prevScreen(kScreenFlights) == kScreenRadar.
int nextScreen(int current);
int prevScreen(int current);

// Stateful current-screen tracker — the "current index + nextScreen()/
// prevScreen()" CLAUDE.md's "Screen navigation" describes. main.cpp keeps
// one, seeds it from config_store's persisted last_screen at boot, and
// re-persists current() whenever a mutator reports a change. Header-only:
// it's just wrapScreen() bookkeeping, still exercised under
// `pio test -e native`.
class ScreenNav {
 public:
  explicit ScreenNav(int initialScreen = kScreenFlights) : current_(wrapScreen(initialScreen)) {}

  int current() const { return current_; }

  // Each returns true iff the active screen actually changed — the signal
  // for the caller to persist last_screen and trigger a redraw.
  bool set(int screen) {
    int next = wrapScreen(screen);
    if (next == current_) return false;
    current_ = next;
    return true;
  }
  bool next() { return set(nextScreen(current_)); }
  bool prev() { return set(prevScreen(current_)); }

 private:
  int current_ = kScreenFlights;
};

// ---- Auto screen cycling ------------------------------------------------
//
// See CLAUDE.md "Auto screen cycling". The auto-cycle timer itself lives
// in main.cpp (a millis() delta from the last screen change); the one
// piece worth isolating and testing here is the "don't interrupt an
// imminent overhead moment" hold condition.
//
// Pure predicate: returns true when an auto-switch should be *deferred*
// because the Flight screen is showing an aircraft that's about to be (or
// just was) overhead. Takes t_cpa_seconds as a parameter rather than
// reaching into cpa_predictor - same decoupling discipline as the rest of
// the project.
//
//   defer  <=>  currentScreen == kScreenFlight
//           &&  cpaFound
//           &&  tCpaSeconds < 6      (strictly less - exactly 6 does NOT hold)
//           &&  tCpaSeconds > -5     (strictly greater - exactly -5 does NOT hold)
//
// In plain terms: less than 6 s until overhead -> hold; keep holding
// through the pass and for 5 s after; tCpaSeconds reaching -5 clears it.
// The 6 / -5 thresholds are fixed, not user-configurable.
constexpr float kAutoSwitchHoldBeforeS = 6.0f;
constexpr float kAutoSwitchHoldAfterS = -5.0f;

bool shouldDeferAutoSwitch(int currentScreen, bool cpaFound, float tCpaSeconds);

// Auto-cycle "advance now?" decision, pure so it's testable without
// main.cpp or a real millis() clock. Inputs:
//   elapsedMs  - millis() elapsed since the last screen change
//   intervalS  - configured auto_cycle_interval_s (config_store)
//   deferHold  - the current shouldDeferAutoSwitch() result
//
//   advance  <=>  elapsedMs >= intervalS * 1000  &&  !deferHold
//
// While deferHold is true this stays false and the caller must NOT reset
// its timer, so the switch fires on the first loop iteration after the
// hold clears - however long that took. Compared in 64-bit so a large
// misconfigured interval can't overflow intervalS * 1000.
bool shouldAutoAdvance(uint32_t elapsedMs, uint32_t intervalS, bool deferHold);

// ---- Touch geometry -------------------------------------------------------
//
// The content area sits between the header (top, `headerHeight` px) and
// the bottom nav bar (`kBottomNavHeight` px at the very bottom). The
// outer ~18% of width on each side, within that content area, are the
// screen-switch edge strips; the middle is left alone for screen-specific
// interactions (e.g. Screen 1's table pagination).

constexpr int16_t kBottomNavHeight = 18;
// Edge strip width as a percentage of screen width, per side.
constexpr int16_t kEdgeStripPercent = 18;

// Left edge strip: tapping it goes to the previous screen.
Rect leftEdgeZone(int16_t screenWidth, int16_t screenHeight, int16_t headerHeight);
// Right edge strip: tapping it goes to the next screen.
Rect rightEdgeZone(int16_t screenWidth, int16_t screenHeight, int16_t headerHeight);

// Full bottom nav bar, and the bounds of one of its `kScreenCount`
// equal-width tappable segments (segment i jumps directly to screen i).
Rect bottomNavBounds(int16_t screenWidth, int16_t screenHeight);
Rect bottomNavSegment(int index, int16_t screenWidth, int16_t screenHeight);

enum class NavAction {
  None,    // tap missed every nav zone — leave it for the active screen
  Prev,    // left edge strip
  Next,    // right edge strip
  JumpTo,  // a bottom nav segment — see NavHit::target for which screen
};

struct NavHit {
  NavAction action = NavAction::None;
  int target = -1;  // valid screen index only when action == JumpTo
};

// Classifies a tap. The bottom nav bar takes priority over the edge
// strips where they'd overlap (the strips stop above the nav bar anyway).
NavHit navHitTest(int16_t x, int16_t y, int16_t screenWidth, int16_t screenHeight,
                  int16_t headerHeight);
