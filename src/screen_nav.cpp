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

// Bottom of the content area — where the edge strips stop and the nav bar
// begins.
int16_t contentBottom(int16_t screenHeight) {
  return static_cast<int16_t>(screenHeight - kBottomNavHeight);
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
  r.y = contentBottom(screenHeight);
  r.w = screenWidth;
  r.h = kBottomNavHeight;
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

  if (hitTest(x, y, bottomNavBounds(screenWidth, screenHeight))) {
    for (int i = 0; i < kScreenCount; ++i) {
      if (hitTest(x, y, bottomNavSegment(i, screenWidth, screenHeight))) {
        hit.action = NavAction::JumpTo;
        hit.target = i;
        return hit;
      }
    }
    return hit;  // inside the bar but between segments (shouldn't happen) — no-op
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
