// screen_nav — which of the three screens (Flights / Flight / Radar) is
// active, cycling with wraparound, plus touch hit-testing for the bottom
// nav bar segments (the only way to switch screens). See CLAUDE.md
// "Screen navigation" and "Visualization concept".
//
// Zero TFT drawing, zero OpenSky/hexdb.io calls — pure index arithmetic
// and rectangle geometry, reusing touch_input's Rect/hitTest. Tested
// under `pio test -e native`.
#pragma once

#include <cstdint>

#include "lcars_theme.h"  // LCARS_HEADER_HEIGHT, LCARS_BOTTOM_NAV_HEIGHT
#include "status_bar.h"   // HeaderZones — the "!" badge's position
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

// Minimum gap between NVS writes of `last_screen` when the screen change
// was NOT user-initiated (i.e. an auto-cycle advance). Manual taps persist
// immediately; auto-cycle — which can fire every few seconds — is
// rate-limited to this so it doesn't wear the flash writing the same key
// thousands of times a day (see CLAUDE.md review notes 5.9). 10 minutes:
// on a reboot you resume within 10 min of where auto-cycle left off,
// which is plenty for a decorative feature.
constexpr uint32_t kScreenPersistMinIntervalMs = 10UL * 60UL * 1000UL;

// Whether to write `last_screen` to NVS right now. A user-initiated change
// always persists; an auto-cycle change persists only if at least
// `minIntervalMs` has passed since the last write. Pure — `msSinceLastPersist`
// is injected — native-tested.
bool shouldPersistScreenChange(bool userInitiated, uint32_t msSinceLastPersist,
                               uint32_t minIntervalMs);

// Why the active screen changed (spec 10 §2.2). A screenshot's switch
// (/screenshot.bmp?screen=N) is temporary and never written to NVS; a touch
// always is; an auto-cycle advance at most every `minIntervalMs`.
enum class ScreenChangeCause : uint8_t { Touch, AutoCycle, Screenshot };
bool shouldPersistScreenChange(ScreenChangeCause cause, uint32_t msSinceLastPersist,
                               uint32_t minIntervalMs);

// ---- Touch geometry -------------------------------------------------------
//
// Screens are switched **only** by the bottom nav buttons. (Left/right edge
// strips that also switched screens were removed on request.) Everything
// above the nav band is left to the active screen (e.g. Screen 1's table
// paging).
//
// The bottom nav bar draws LCARS_BOTTOM_NAV_HEIGHT (25) px tall
// (y:215..240 on the 240px panel); navHitTest() accepts taps in a slightly
// taller band, y:210..240, for an imprecise finger.
constexpr int16_t kBottomNavTouchExtra = 5;

// The bottom nav bar as it is *drawn* (y:215..240, LCARS_BOTTOM_NAV_HEIGHT
// tall), and the bounds of one of its `kScreenCount` equal-width segments
// (segment i jumps directly to screen i). navHitTest() accepts taps in a
// taller band than this — see kBottomNavTouchExtra.
Rect bottomNavBounds(int16_t screenWidth, int16_t screenHeight);
Rect bottomNavSegment(int index, int16_t screenWidth, int16_t screenHeight);

// ---- Tab look of the bottom nav (drawing geometry only) -------------------
//
// The nav is drawn as three folder tabs hanging from a peach rule along the
// bar's top edge: the active tab is full height and merges into the rule
// (the "pulled-out" sheet), inactive tabs sit lower, recessed behind it.
// Touch zones are unaffected — navHitTest() still uses bottomNavSegment().
constexpr int16_t kNavTabRulePx = 2;          // rule thickness, y:215..217
constexpr int16_t kNavTabInactiveDropPx = 2;  // gap between rule and an inactive tab
constexpr int16_t kNavTabOverlapPx = 2;       // active tab overlaps each neighbour
constexpr int16_t kNavTabGapPx = 1;           // inset of inactive tabs (separates them)
constexpr int16_t kNavTabCornerRadius = 6;    // bottom corners only; top is square

// The full-width rule the tabs hang from (top kNavTabRulePx rows of the bar).
Rect navTabRule(int16_t screenWidth, int16_t screenHeight);

// Rectangle of tab `index` (rounded bottom corners, square top), drawn
// `active` or not. Active: starts at the bar top (merging with the rule),
// widened by kNavTabOverlapPx each side (clamped to the screen). Inactive:
// starts kNavTabInactiveDropPx below the rule, inset by kNavTabGapPx.
// Out-of-range index -> empty rect. Pure — native-tested.
Rect navTabRect(int index, bool active, int16_t screenWidth, int16_t screenHeight);

enum class NavAction {
  None,    // not a nav-button tap — leave it for the active screen
  JumpTo,  // a bottom nav segment — see NavHit::target for which screen
};

struct NavHit {
  NavAction action = NavAction::None;
  int target = -1;  // valid screen index only when action == JumpTo
};

// Classifies a tap: JumpTo the segment's screen inside the bottom nav touch
// band (y:210..240), None everywhere else.
NavHit navHitTest(int16_t x, int16_t y, int16_t screenWidth, int16_t screenHeight);

// ---- The device-alert "!" in the header (spec 08 §3.5) ---------------------
//
// Tapping it opens the message screen. The zone is more generous than the
// 13 px badge — resistive touch is imprecise at the edge (see "Touch
// calibration"): from the left screen edge to kAlertBadgeTouchSlackX px past
// the badge, and from the top down to y = kAlertBadgeTouchBottom (the header
// + 9 px). Far from the bottom nav (y >= 210) and the table's paging strip
// (first row at y:116). Checked in handleTap() before anything else.
constexpr int kAlertBadgeTouchSlackX = 8;
constexpr int kAlertBadgeTouchBottom = 24;  // inclusive

// True for a tap on the badge's zone; false whenever no badge is drawn
// (zones.alert_x < 0).
bool alertBadgeHit(int16_t x, int16_t y, const HeaderZones &zones);
