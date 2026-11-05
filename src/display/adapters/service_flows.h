// service_flows — the device halves of the UI's service flows
// (display/services: ota_screen_flow, update_prompt_flow, alert_screen_flow,
// password_reset_flow): read the touch, take one step of the pure flow, draw
// what the step asks for (service_screen_draw) and set the backlight; and
// ServiceScreens, the three screens of the UI loop together
// (service_screen_flows). Not under lgfx/ because they read the touch and
// drive the backlight too.
// Hardware adapter, not covered by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/services/alert_screen_flow.h"
#include "display/services/ota_screen_flow.h"
#include "display/services/password_reset_flow.h"
#include "display/services/service_screen_flows.h"
#include "display/services/update_prompt_flow.h"

// The firmware-update screen from NetState::ota: full brightness and the
// frame on a new update, then the bar / the result. `installedVersion` is
// the version line's "from".
OtaScreenStep runOtaScreen(LGFX &tft, OtaScreenFlow &flow, const OtaProgress &o,
                           bool restartPending, const char *installedVersion);

// The update question was just opened (UpdatePromptFlow::tryOpen()): the log
// line and the screen.
void openUpdatePrompt(LGFX &tft, const UpdatePromptFlow &flow, const std::string &installed);
// One pass while it's open: reads the touch, steps, redraws the timer strip.
// The caller sends the step's command and repaints on firstDraw.
PromptStep runUpdatePrompt(LGFX &tft, UpdatePromptFlow &flow);

// Opens the alert message screen behind the header's "!" (log line + screen).
void showAlertScreen(LGFX &tft, AlertScreenFlow &flow, const AlertSet &alerts,
                     const AlertDetails &details);
// One pass while it's open: reads the touch, steps, redraws the list or the
// CLOSE countdown. On close the caller repaints the normal screens.
AlertFlowStep runAlertScreen(LGFX &tft, AlertScreenFlow &flow, const AlertSet &alerts,
                             const AlertDetails &details);

// At power-up, before net_task (blocking, delay() between steps): with a
// password set and a finger on the screen, the hold / confirm screens of
// password_reset_flow. On YES `clearPassword` runs (the caller clears and
// saves it) before "password removed" is shown.
void runPasswordReset(LGFX &tft, bool passwordSet, const std::function<void()> &clearPassword);

// The OTA screen, the update question and the alert message as loop() runs
// them: each pass reads the touch, steps the flows, draws, and answers with
// one ServiceOutcome (service_screen_flows decides). YES / NO go to net_task
// through core_link::sendCommand.
class ServiceScreens : public ServiceScreenFlows {
 public:
  ServiceScreens();
  // The OTA screen: first in every pass.
  ServiceOutcome otaPass(LGFX &tft, const NetState &s, const char *installedVersion);
  // After a screenshot: the update question, then the alert message (each only while up).
  ServiceOutcome dialogsPass(LGFX &tft, const NetState &s);
  // The header's "!" was tapped.
  void openAlerts(LGFX &tft, const NetState &s);
  // On the normal screens: asks the update question when it may (once per boot).
  void askAboutUpdateIfDue(LGFX &tft, const NetState &s, int currentScreen, bool touchPressed,
                           bool screenOff);
};
