// service_screen — the full-screen "maintenance" displays that replace the
// normal screens for a moment (spec/01-ota-updates.md):
//   - firmware update in progress: title, "v1.2.0 -> v1.3.0", progress bar,
//     percent (or KB when the size isn't known), "do not power off";
//   - resetting the web panel password from the touchscreen at power-up:
//     "keep holding" with a filling bar, then YES / NO buttons.
//
// Geometry and text are pure (tested under `pio test -e native`); the draw
// calls below the ARDUINO guard are the thin LovyanGFX adapter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "touch_input.h"  // Rect
#include "web_auth.h"     // ResetChoice

// The progress bar (OTA and the reset hold), centred, 240 x 14 px.
Rect serviceProgressBarRect(int16_t screenW, int16_t screenH);
// Filled width for `percent` (clamped 0..100) of a `barW`-wide bar; -1
// (unknown) fills nothing.
int16_t serviceProgressFillPx(int percent, int16_t barW);

// "45%" when the total is known, else "812 KB".
std::string otaProgressText(size_t written, size_t total);
// "v1.2.0 → v1.3.0"; just the target while the old one is unknown; "" when
// the new version isn't known yet (its marker comes late in the file).
std::string otaVersionLine(const std::string &from, const std::string &to);

// YES (left) / NO (right) buttons of the reset confirmation, and which one
// a tap hit.
Rect resetButtonRect(bool yes, int16_t screenW, int16_t screenH);
ResetChoice resetConfirmHit(int16_t x, int16_t y, int16_t screenW, int16_t screenH);

#ifdef ARDUINO

#include "LGFX_CYD.hpp"

// Clears the screen and draws the static part of the update screen.
void drawOtaScreen(LGFX &gfx, const std::string &fromVersion);
// Repaints only the version line, bar and percent.
void drawOtaProgress(LGFX &gfx, const std::string &fromVersion, const std::string &toVersion,
                     size_t written, size_t total);
// "DONE · RESTARTING…" or "UPDATE FAILED" in place of the progress text.
void drawOtaResult(LGFX &gfx, bool ok);

void drawResetHoldScreen(LGFX &gfx);
void drawResetHoldProgress(LGFX &gfx, uint32_t heldMs);
void drawResetConfirmScreen(LGFX &gfx);
void drawResetDoneScreen(LGFX &gfx);

#endif  // ARDUINO
