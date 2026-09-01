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
