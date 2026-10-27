#include "display/domain/redraw_policy.h"

#include "display/domain/screen_nav.h"  // kScreenFlight

uint8_t computeRedrawMask(const RedrawEvents &e, int currentScreen) {
  if (e.firstDraw || e.screenChanged || e.pageChanged || e.languageChanged) {
    return kRedrawFull;
  }

  uint8_t mask = 0;
  if (e.newPoll) mask |= kRedrawContent | kRedrawHeader;
  if (e.minuteChanged || e.healthChanged || e.alertsChanged) mask |= kRedrawHeader;
  if (e.countdownTick && currentScreen == kScreenFlight && !(mask & kRedrawContent)) {
    mask |= kRedrawCountdown;
  }
  return mask;
}
