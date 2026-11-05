// service_flows — the device halves of the UI's service flows
// (display/services: ota_screen_flow, update_prompt_flow, alert_screen_flow,
// password_reset_flow): read the touch, take one step of the pure flow, draw
// what the step asks for (service_screen_draw) and set the backlight. Not
// under lgfx/ because they read the touch and drive the backlight too.
// Hardware adapter, not covered by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>
#include <string>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/services/alert_screen_flow.h"
#include "display/services/ota_screen_flow.h"
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
