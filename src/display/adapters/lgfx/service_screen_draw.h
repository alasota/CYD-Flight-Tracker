// service_screen_draw — the LovyanGFX half of service_screen: the full-screen
// OTA, password-reset, update-question and device-alert displays, drawn from
// service_screen's pure geometry and texts. Hardware adapter, not covered by
// Unity (CLAUDE.md "Testing").
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/screens/service_screen.h"

// Clears the screen and draws the static part of the update screen; the
// title says "downloading from GitHub" for release_check's installs.
void drawOtaScreen(LGFX &gfx, const std::string &fromVersion, bool fromGithub = false);
// Repaints only the version line, bar and percent.
void drawOtaProgress(LGFX &gfx, const std::string &fromVersion, const std::string &toVersion,
                     size_t written, size_t total);
// "DONE · RESTARTING…" or "UPDATE FAILED" in place of the progress text.
void drawOtaResult(LGFX &gfx, bool ok);

void drawResetHoldScreen(LGFX &gfx);
void drawResetHoldProgress(LGFX &gfx, uint32_t heldMs);
void drawResetConfirmScreen(LGFX &gfx);
void drawResetDoneScreen(LGFX &gfx);

// The update question, and the strip under the buttons that runs out as
// `remainingMs` of kUpdatePromptTimeoutMs pass.
void drawUpdatePromptScreen(LGFX &gfx, const std::string &fromVersion, const std::string &toVersion);
void drawUpdatePromptTimer(LGFX &gfx, uint32_t remainingMs);

// The alert message screen (full screen), and only its CLOSE button with the
// countdown (the 1 Hz repaint). `nowMs` dates the quota's renewal time.
void drawAlertScreen(LGFX &gfx, AlertSet alerts, const AlertDetails &details, uint32_t nowMs,
                     uint32_t secondsLeft);
void drawAlertCloseButton(LGFX &gfx, int count, uint32_t secondsLeft);
