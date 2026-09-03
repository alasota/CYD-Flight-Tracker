#include "screen_nav.h"

int wrapScreen(int index) {
  int m = index % kScreenCount;
  if (m < 0) m += kScreenCount;
  return m;
}

int nextScreen(int current) { return wrapScreen(current + 1); }
int prevScreen(int current) { return wrapScreen(current - 1); }

namespace {

int16_t edgeStripWidth(int16_t screenWidth) {
  return static_cast<int16_t>(static_cast<int>(screenWidth) * kEdgeStripPercent / 100);
}

// Bottom of the content area — where the edge strips stop (also the top
// of the *visually drawn* nav bar): y:225 on a 240px panel.
int16_t contentBottom(int16_t screenHeight) {
  return static_cast<int16_t>(screenHeight - LCARS_BOTTOM_NAV_HEIGHT);
}

// Top of the nav bar's touch band — kBottomNavTouchExtra px above where
// the bar draws, so a 15px bar isn't a painful tap target (y:215).
int16_t navTouchTop(int16_t screenHeight) {
  return static_cast<int16_t>(screenHeight - LCARS_BOTTOM_NAV_HEIGHT - kBottomNavTouchExtra);
}

}  // namespace

Rect leftEdgeZone(int16_t screenWidth, int16_t screenHeight, int16_t headerHeight) {
  Rect r;
  r.x = 0;
  r.y = headerHeight;
  r.w = edgeStripWidth(screenWidth);
  r.h = static_cast<int16_t>(contentBottom(screenHeight) - headerHeight);
  return r;
}

Rect rightEdgeZone(int16_t screenWidth, int16_t screenHeight, int16_t headerHeight) {
  Rect r;
  int16_t w = edgeStripWidth(screenWidth);
  r.x = static_cast<int16_t>(screenWidth - w);
  r.y = headerHeight;
  r.w = w;
  r.h = static_cast<int16_t>(contentBottom(screenHeight) - headerHeight);
  return r;
}

Rect bottomNavBounds(int16_t screenWidth, int16_t screenHeight) {
  Rect r;
  r.x = 0;
  r.y = static_cast<int16_t>(screenHeight - LCARS_BOTTOM_NAV_HEIGHT);  // y:225
  r.w = screenWidth;
  r.h = LCARS_BOTTOM_NAV_HEIGHT;  // draws 15px; navHitTest() accepts a taller band
  return r;
}

Rect bottomNavSegment(int index, int16_t screenWidth, int16_t screenHeight) {
  Rect r = bottomNavBounds(screenWidth, screenHeight);
  if (index < 0 || index >= kScreenCount) {
    r.w = 0;
    r.h = 0;
    return r;
  }
  int16_t segW = static_cast<int16_t>(screenWidth / kScreenCount);
  r.x = static_cast<int16_t>(index * segW);
  // Last segment absorbs any rounding remainder so the bar is fully covered.
  r.w = (index == kScreenCount - 1) ? static_cast<int16_t>(screenWidth - r.x) : segW;
  return r;
}

NavHit navHitTest(int16_t x, int16_t y, int16_t screenWidth, int16_t screenHeight,
                  int16_t headerHeight) {
  NavHit hit;

  // Bottom nav bar first — its touch band (y:215..240) is taller than the
  // 15px it draws, and it wins the y:215..225 overlap with the edge
  // strips. Segment is picked by x only (the whole band is the bar).
  if (y >= navTouchTop(screenHeight) && y < screenHeight && x >= 0 && x < screenWidth) {
    for (int i = 0; i < kScreenCount; ++i) {
      Rect seg = bottomNavSegment(i, screenWidth, screenHeight);
      if (x >= seg.x && x < static_cast<int16_t>(seg.x + seg.w)) {
        hit.action = NavAction::JumpTo;
        hit.target = i;
        return hit;
      }
    }
    return hit;  // in the band but between segments (shouldn't happen) — no-op
  }

  if (hitTest(x, y, leftEdgeZone(screenWidth, screenHeight, headerHeight))) {
    hit.action = NavAction::Prev;
    return hit;
  }
  if (hitTest(x, y, rightEdgeZone(screenWidth, screenHeight, headerHeight))) {
    hit.action = NavAction::Next;
    return hit;
  }

  return hit;  // NavAction::None
}

bool shouldDeferAutoSwitch(int currentScreen, bool cpaFound, float tCpaSeconds) {
  if (currentScreen != kScreenFlight) return false;
  if (!cpaFound) return false;
  return tCpaSeconds < kAutoSwitchHoldBeforeS && tCpaSeconds > kAutoSwitchHoldAfterS;
}

bool shouldAutoAdvance(uint32_t elapsedMs, uint32_t intervalS, bool deferHold) {
  if (deferHold) return false;
  const uint64_t intervalMs = static_cast<uint64_t>(intervalS) * 1000ULL;
  return static_cast<uint64_t>(elapsedMs) >= intervalMs;
}

bool shouldPersistScreenChange(bool userInitiated, uint32_t msSinceLastPersist,
                               uint32_t minIntervalMs) {
  if (userInitiated) return true;
  return msSinceLastPersist >= minIntervalMs;
}
