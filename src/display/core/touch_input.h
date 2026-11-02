// touch_input — the pure side of touch: a generic point-in-rectangle hit
// test shared by screen_nav's bottom-nav buttons and table_view's page tap
// zones, and the raw -> screen mapping model the calibration is tested with.
// Reading the panel (LovyanGFX's built-in XPT2046 driver, configured in
// display/adapters/lgfx/LGFX_CYD.hpp) is display/adapters/touch_xpt2046.
// See CLAUDE.md "Hardware" and "Code conventions".
#pragma once

#include <cstdint>

// ---- Pure logic (no LovyanGFX/TFT dependency) — testable under
// `pio test -e native`.

// A simple screen-space rectangle (top-left x/y, width, height) — the
// shared "tappable region" type used across the UI.
struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
};

// True if (touchX, touchY) falls within `bounds`, inclusive of its edges.
// Pure — no TFT dependency — tested under `pio test -e native`.
bool hitTest(int16_t touchX, int16_t touchY, const Rect &bounds);

// Maps one raw touch ADC reading onto a screen-pixel axis: clamps `raw` to
// [inMin, inMax], then linearly rescales it to [outMin, outMax] (outMin >
// outMax inverts). A degenerate range (inMin == inMax) returns `outMin`
// instead of dividing by zero.
int16_t mapTouchAxis(int32_t raw, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax);

// A tap position in screen pixels (landscape, 0,0 = top-left).
struct TouchXY {
  int16_t x = 0;
  int16_t y = 0;
};

// Model of what LovyanGFX's getTouch() computes from a raw LovyanGFX touch
// sample (`rawX`, `rawY` as tft.getTouchRaw() reports them) with this
// board's calibration (touch_calibration.h), offset_rotation 0 and
// tft.setRotation(1): raw Y -> screen x (inverted: high raw = left), raw X -> screen y,
// flipped (raw X grows towards the top). The device uses LovyanGFX's own
// conversion; this pure copy exists so the native tests can check the
// calibration constants against raw taps logged on the real board.
TouchXY mapRawTouch(int32_t rawX, int32_t rawY);
