#include "display/services/alert_screen_flow.h"

uint32_t AlertScreenFlow::open(uint32_t nowMs, const AlertSet &alerts) {
  screen_ = openAlertScreen(nowMs);
  drawn_ = alerts;
  return alertScreenSecondsLeft(screen_, nowMs);
}

AlertFlowStep AlertScreenFlow::step(const AlertScreenInput &in, const AlertSet &alerts) {
  AlertFlowStep r;
  if (!screen_.open) return r;
  r.swallowTouch = true;  // while open and on the pass that closes it
  const AlertScreenStepResult s = alertScreenStep(screen_, in);
  screen_ = s.state;
  if (s.close) {
    screen_.open = false;
    r.close = true;
    return r;
  }
  if (alerts != drawn_) {
    // One came or went: the list again, the countdown runs on.
    drawn_ = alerts;
    r.redrawList = true;
    r.secondsLeft = alertScreenSecondsLeft(screen_, in.nowMs);
  } else if (s.redrawTimer) {
    r.redrawTimer = true;
    r.secondsLeft = screen_.secondsShown;
  }
  return r;
}

void AlertScreenFlow::closeQuietly() { screen_.open = false; }

bool AlertScreenFlow::isOpen() const { return screen_.open; }

uint32_t AlertScreenFlow::msToNextSecond(uint32_t nowMs) const {
  return alertScreenMsToNextSecond(screen_, nowMs);
}
