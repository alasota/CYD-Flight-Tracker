// touch_input — XPT2046 touchscreen reading + raw-to-screen coordinate
// mapping, plus a generic point-in-rectangle hit test shared by
// lcars_theme's view-toggle button and table_view's page tap zones. See
// CLAUDE.md "Hardware" (XPT2046_Touchscreen, on its own SPI bus from the
// display) and "Code conventions".
#pragma once

#include <cstdint>

// ---- Pure logic (no XPT2046/TFT dependency) — testable under
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
// Pure — no XPT2046/TFT dependency — tested under `pio test -e native`.
bool hitTest(int16_t touchX, int16_t touchY, const Rect &bounds);

// Maps one raw XPT2046 ADC reading onto a screen-pixel axis: clamps `raw`
// to [inMin, inMax], then linearly rescales it to [outMin, outMax]. A
// degenerate calibration range (inMin == inMax, e.g. never-tuned
// placeholder values that ended up equal) returns `outMin` instead of
// dividing by zero. Hoisted out of the XPT2046 adapter specifically so
// the calibration arithmetic — the only non-trivial logic in this module
// besides hitTest() — is covered by `pio test -e native` (see CLAUDE.md
// "Testing": "if a function is pure enough to unit test, it needs one").
int16_t mapTouchAxis(int32_t raw, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax);

// ---- Hardware adapter: reads the XPT2046 touchscreen (its own SPI bus,
// separate from the display — see CLAUDE.md pinout) and maps its raw ADC
// coordinates to TFT screen coordinates. Not covered by Unity (see
// CLAUDE.md "Testing"). No TFT drawing here, just touch reading.
#ifdef ARDUINO

// One touch sample, already mapped to screen coordinates (0,0 = top-left,
// matching the LGFX rotation set in main.cpp).
struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
};

// Initializes the XPT2046 touch controller on its own SPI bus (pins per
// CLAUDE.md's pinout table: CS 33, IRQ 36, MOSI 32, MISO 39, CLK 25).
// Call once from setup().
void touchInputBegin();

// Polls the touch controller and maps the result to screen coordinates.
// Non-blocking — safe to call every loop() iteration. `pressed` is false
// (x/y meaningless) when nothing is currently touching the panel.
TouchPoint touchInputRead();

#endif  // ARDUINO
