// service_screen — the full-screen "maintenance" displays that replace the
// normal screens for a moment (spec/done/01-ota-updates.md):
//   - firmware update in progress: title, "v1.2.0 -> v1.3.0", progress bar,
//     percent (or KB when the size isn't known), "do not power off";
//   - resetting the web panel password from the touchscreen at power-up:
//     "keep holding" with a filling bar, then YES / NO buttons;
//   - "NEW VERSION AVAILABLE v1.2.0 → v1.3.0 — INSTALL IT?" with YES / NO
//     and a bar running out over 30 s (release_check, spec/02);
//   - the device-alert message screen behind the header's "!": what is
//     wrong and what to do, CLOSE with a 15 s countdown (spec 08 §3.5).
//
// Geometry and text are pure (tested under `pio test -e native`); the draw
// calls below the ARDUINO guard are the thin LovyanGFX adapter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "device_alerts.h"  // AlertSet, AlertDetails
#include "touch_input.h"    // Rect
#include "web_auth.h"       // ResetChoice

// The progress bar (OTA and the reset hold), centred, 240 x 14 px.
Rect serviceProgressBarRect(int16_t screenW, int16_t screenH);
// Filled width for `percent` (clamped 0..100) of a `barW`-wide bar; -1
// (unknown) fills nothing.
int16_t serviceProgressFillPx(int percent, int16_t barW);

// Width of the update prompt's countdown strip, `barW` wide when the full
// `timeoutMs` remains, 0 once it has run out.
int16_t promptTimerFillPx(uint32_t remainingMs, uint32_t timeoutMs, int16_t barW);

// "45%" when the total is known, else "812 KB".
std::string otaProgressText(size_t written, size_t total);
// "v1.2.0 → v1.3.0"; just the target while the old one is unknown; "" when
// the new version isn't known yet (its marker comes late in the file).
std::string otaVersionLine(const std::string &from, const std::string &to);

// YES (left) / NO (right) buttons of a confirmation (password reset,
// update prompt), and which one a tap hit.
Rect confirmButtonRect(bool yes, int16_t screenW, int16_t screenH);
ResetChoice confirmHit(int16_t x, int16_t y, int16_t screenW, int16_t screenH);

// ---- Device-alert message screen (spec 08 §3.5) ------------------------------

// At most this many rows of entries; with more alerts the last row says
// "+ N more — see the web panel" instead.
constexpr int kAlertScreenMaxRows = 3;
constexpr uint32_t kAlertScreenTimeoutMs = 15000;
constexpr int16_t kAlertCloseButtonMinH = 36;

struct AlertScreenLayout {
  Rect title;                          // "(!) ATTENTION", y 8..36
  Rect entries[kAlertScreenMaxRows];   // icon + title, body below it
  int shown = 0;                       // entries drawn (<= kAlertScreenMaxRows)
  int more = 0;                        // > 0: the last row is "+ more" for this many
  Rect moreRow;                        // valid when more > 0
  Rect hint;                           // "Details: http://cyd-sky.local"
  Rect button;                         // CLOSE + countdown
};
// Where everything goes for `count` alerts on a w x h screen. Pure.
AlertScreenLayout alertScreenLayout(int16_t w, int16_t h, int count);

// The screen's own state: opened when, and the "fresh tap" bookkeeping (a
// tap counts only after the finger that opened it was lifted, like the
// update question's acceptPromptTap()).
struct AlertScreen {
  bool open = false;
  uint32_t openedMs = 0;
  bool released = false;    // finger lifted since it opened
  bool wasPressed = true;   // the opening tap is still down
  uint32_t secondsShown = 0;  // countdown on the button as last drawn
};
struct AlertScreenInput {
  uint32_t nowMs = 0;
  bool pressed = false;
  int16_t x = 0, y = 0;
  bool anyDisplayAlerts = true;  // AlertSet::displaySeverity() != None
  int16_t screenW = 320, screenH = 240;
};
struct AlertScreenStepResult {
  AlertScreen state;
  bool close = false;        // back to the normal screens (full redraw)
  bool redrawTimer = false;  // the countdown on the button changed
};

AlertScreen openAlertScreen(uint32_t nowMs);
// Whole seconds left on the CLOSE button: 15 right after opening .. 1 in the
// last second, 0 once the time is up. Wrap-safe.
uint32_t alertScreenSecondsLeft(const AlertScreen &s, uint32_t nowMs);
// Ms until that number changes (the UI's next wake-up). Wrap-safe.
uint32_t alertScreenMsToNextSecond(const AlertScreen &s, uint32_t nowMs);
// One UI pass while it's open: closes on a fresh tap on CLOSE, after
// kAlertScreenTimeoutMs (taps elsewhere change nothing, don't extend it), or
// at once when no display alert is left.
AlertScreenStepResult alertScreenStep(const AlertScreen &s, const AlertScreenInput &in);

// "ZAMKNIJ  15 s" / "CLOSE  15 s".
std::string alertCloseLabel(uint32_t secondsLeft, Language lang);

#ifdef ARDUINO

#include "LGFX_CYD.hpp"

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

#endif  // ARDUINO
