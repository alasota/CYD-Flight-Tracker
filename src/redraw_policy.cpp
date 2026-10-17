#include "redraw_policy.h"

#include "screen_nav.h"  // kScreenFlight

uint8_t computeRedrawMask(const RedrawEvents &e, int currentScreen) {
  if (e.firstDraw || e.screenChanged || e.pageChanged || e.languageChanged) {
    return kRedrawFull;
  }

  uint8_t mask = 0;
  if (e.newPoll) mask |= kRedrawContent;
  if (e.minuteChanged || e.healthChanged) mask |= kRedrawHeaderText;
  if (e.countdownTick && currentScreen == kScreenFlight && !(mask & kRedrawContent)) {
    mask |= kRedrawCountdown;
  }
  return mask;
}
