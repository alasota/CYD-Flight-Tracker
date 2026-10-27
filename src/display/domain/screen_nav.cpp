#include "display/domain/screen_nav.h"

int wrapScreen(int index) {
  int m = index % kScreenCount;
  if (m < 0) m += kScreenCount;
  return m;
}

int nextScreen(int current) { return wrapScreen(current + 1); }
int prevScreen(int current) { return wrapScreen(current - 1); }

namespace {

// Top of the nav bar's touch band — kBottomNavTouchExtra px above where
// the bar draws, for an imprecise finger (y:210).
int16_t navTouchTop(int16_t screenHeight) {
  return static_cast<int16_t>(screenHeight - LCARS_BOTTOM_NAV_HEIGHT - kBottomNavTouchExtra);
}

}  // namespace

Rect bottomNavBounds(int16_t screenWidth, int16_t screenHeight) {
  Rect r;
  r.x = 0;
  r.y = static_cast<int16_t>(screenHeight - LCARS_BOTTOM_NAV_HEIGHT);  // y:215
  r.w = screenWidth;
  r.h = LCARS_BOTTOM_NAV_HEIGHT;  // draws 25px; navHitTest() accepts a taller band
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

Rect navTabRule(int16_t screenWidth, int16_t screenHeight) {
  Rect r = bottomNavBounds(screenWidth, screenHeight);
  r.h = kNavTabRulePx;
  return r;
}

Rect navTabRect(int index, bool active, int16_t screenWidth, int16_t screenHeight) {
  Rect r = bottomNavSegment(index, screenWidth, screenHeight);
  if (r.w == 0) return r;  // out of range
  const int16_t bottom = static_cast<int16_t>(r.y + r.h);
  if (active) {
    int16_t left = static_cast<int16_t>(r.x - kNavTabOverlapPx);
    int16_t right = static_cast<int16_t>(r.x + r.w + kNavTabOverlapPx);
    if (left < 0) left = 0;
    if (right > screenWidth) right = screenWidth;
    r.x = left;
    r.w = static_cast<int16_t>(right - left);
    return r;  // full height: y from the bar top, merging with the rule
  }
  r.x = static_cast<int16_t>(r.x + kNavTabGapPx);
  r.w = static_cast<int16_t>(r.w - 2 * kNavTabGapPx);
  r.y = static_cast<int16_t>(r.y + kNavTabRulePx + kNavTabInactiveDropPx);
  r.h = static_cast<int16_t>(bottom - r.y);
  return r;
}

NavHit navHitTest(int16_t x, int16_t y, int16_t screenWidth, int16_t screenHeight) {
  NavHit hit;

  // Only the bottom nav bar navigates — its touch band (y:210..240) is a
  // little taller than the 25px it draws. Segment is picked by x only (the
  // whole band is the bar).
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

bool shouldPersistScreenChange(ScreenChangeCause cause, uint32_t msSinceLastPersist,
                               uint32_t minIntervalMs) {
  switch (cause) {
    case ScreenChangeCause::Touch:
      return true;
    case ScreenChangeCause::AutoCycle:
      return msSinceLastPersist >= minIntervalMs;
    case ScreenChangeCause::Screenshot:
      return false;  // only for the capture, the screen goes back afterwards
  }
  return false;
}

bool alertBadgeHit(int16_t x, int16_t y, const HeaderZones &zones) {
  if (zones.alert_x < 0) return false;
  const int right = zones.alert_x + kAlertBadgeDiameter + kAlertBadgeTouchSlackX;
  return x >= 0 && x <= right && y >= 0 && y <= kAlertBadgeTouchBottom;
}
