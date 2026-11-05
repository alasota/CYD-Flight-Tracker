// alert_screen_flow — the device-alert message screen behind the header's
// "!" (spec 08 §3.5) as the UI loop runs it: around service_screen's pure
// AlertScreen / alertScreenStep() it keeps which alerts the list shows
// (redraws it when one comes or goes, the countdown runs on), closes it for
// the OTA screen, and tells the loop to swallow the touch while it's open and
// on the pass that closed it.
//
// Pure — tested under `pio test -e native`; the device half (touch read,
// drawing) is display/adapters/service_flows.
#pragma once

#include <cstdint>

#include "display/core/screens/service_screen.h"  // AlertScreen, alertScreenStep(), AlertSet

struct AlertFlowStep {
  bool close = false;        // back to the normal screens: firstDraw, a fresh auto-cycle
  bool redrawList = false;   // drawAlertScreen(): the alerts changed
  bool redrawTimer = false;  // drawAlertCloseButton(): the countdown changed
  uint32_t secondsLeft = 0;  // on the CLOSE button
  bool swallowTouch = false;  // the finger on the screen is no tap for the screen below
};

class AlertScreenFlow {
 public:
  // Opens the screen showing `alerts`; returns the seconds for its CLOSE button.
  uint32_t open(uint32_t nowMs, const AlertSet &alerts);
  // One UI pass while it's open (`alerts` = NetState::alerts now).
  AlertFlowStep step(const AlertScreenInput &in, const AlertSet &alerts);
  // The OTA screen comes first: closed, nothing redrawn.
  void closeQuietly();
  bool isOpen() const;
  // Ms until the CLOSE countdown changes (UiWake::untilAlertTimerMs), while open.
  uint32_t msToNextSecond(uint32_t nowMs) const;

 private:
  AlertScreen screen_;
  AlertSet drawn_;  // what its list shows
};
