// touch_xpt2046 — reads the CYD's XPT2046 resistive touch through LovyanGFX
// (the driver is configured in display/adapters/lgfx/LGFX_CYD.hpp). Hardware
// adapter, not covered by Unity (CLAUDE.md "Testing"); the pure hit test and
// raw -> screen model are touch_input.
#pragma once

#include <cstdint>

#include "display/adapters/lgfx/LGFX_CYD.hpp"

// One touch sample, already in screen coordinates (0,0 = top-left, matching
// the rotation set with tft.setRotation()).
struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
  int16_t raw_x = 0;  // LovyanGFX raw readings — for calibration logging
  int16_t raw_y = 0;
};

// Polls the touch controller through `gfx` (LovyanGFX getTouch()).
// Non-blocking — safe to call every loop() iteration. `pressed` is false
// (x/y meaningless) when nothing is touching the panel. No separate init:
// tft.init() brings up the touch controller too.
TouchPoint touchInputRead(LGFX &gfx);

// One serial line per tap: the raw XPT2046 values, where they mapped to and
// the nav button there — what touch calibration is tuned from
// (display/core/touch_calibration.h).
void touchLogTap(LGFX &gfx, const TouchPoint &t);
