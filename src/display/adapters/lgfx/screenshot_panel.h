// screenshot_panel — the UI core's half of /screenshot.bmp (spec 09 §1, spec
// 10 §2.2): the ScreenshotBridge instance (SpinLock), the four plain
// functions web_server's ScreenshotSource calls on core 0, one UI pass that
// answers the handler's job (screenshotUiPass(), reading rows off the panel
// with readRect()) and the two demo screens a ?screen=N capture can ask for.
// Hardware adapter, not covered by Unity (CLAUDE.md "Testing"); the hand-over
// itself is screenshot_bridge.
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/services/screenshot_bridge.h"

// ---- core 0 (web_server's handler): ScreenshotSource's function pointers
bool screenshotPanelBegin(int screenIndex, uint32_t timeoutMs);
void screenshotPanelRequestBand(int y0, int rows, uint32_t stride, uint8_t *out);
bool screenshotPanelWaitBand(uint32_t timeoutMs);
void screenshotPanelEnd();

// ---- core 1 (loop())

// One UI pass of a capture: a Prepare job calls prepare(index) (the caller
// switches the screen / draws a demo and repaints), a Band job reads its rows
// off the panel. backTo >= 0: the screen shown before the capture, to switch
// back to now.
ScreenshotPassResult screenshotPanelPass(LGFX &tft, int currentScreen, int screenCount,
                                         void (*prepare)(int screenIndex));

// The update question or the alert message screen, drawn from demo data
// (docs/screenshots/update_prompt.png, alert_screen.png).
void drawScreenshotDemo(LGFX &tft, ScreenshotPrepareKind kind);
