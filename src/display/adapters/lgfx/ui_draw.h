// ui_draw — the normal screens as loop() repaints them (CLAUDE.md "Redraw
// policy"): the redraw_policy regions of the active screen — all of it, the
// content band, the header, the NOW countdown — drawn from one UiView
// (ui_flow), and the setup prompt shown while home isn't set. Hardware
// adapter, not covered by Unity (CLAUDE.md "Testing").
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/services/ui_flow.h"  // UiView

// Repaints exactly the regions in `mask` (redraw_policy bits); a full redraw
// clears the screen first, the content band is cleared with one fillRect().
void applyRedraw(LGFX &tft, uint8_t mask, const UiView &v);

// Shown instead of a screen while config_store's home_configured is still
// false (docs/review-notes-2026.md 1.5/1.6).
void drawSetupPrompt(LGFX &tft);
