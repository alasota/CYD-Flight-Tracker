#include "touch_input.h"

bool hitTest(int16_t touchX, int16_t touchY, const Rect &bounds) {
  return touchX >= bounds.x && touchX <= bounds.x + bounds.w && touchY >= bounds.y &&
         touchY <= bounds.y + bounds.h;
}

#ifdef ARDUINO

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

namespace {

// Touch pins per CLAUDE.md's pinout table (separate SPI bus from the
// display's own LovyanGFX bus).
constexpr int kTouchCsPin = 33;
constexpr int kTouchIrqPin = 36;
constexpr int kTouchMosiPin = 32;
constexpr int kTouchMisoPin = 39;
constexpr int kTouchClkPin = 25;

XPT2046_Touchscreen touch(kTouchCsPin, kTouchIrqPin);

// Raw XPT2046 ADC range -> screen pixel range. These are placeholder
// calibration values (typical ballpark for this panel, not measured) —
// touch response WILL be off until they're tuned against real hardware,
// per CLAUDE.md: TFT/touch setups are notoriously fiddly, don't assume.
constexpr int32_t kRawXMin = 200;
constexpr int32_t kRawXMax = 3900;
constexpr int32_t kRawYMin = 200;
constexpr int32_t kRawYMax = 3900;

constexpr int16_t kScreenWidth = 320;
constexpr int16_t kScreenHeight = 240;

int16_t mapRange(int32_t v, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax) {
  if (v < inMin) v = inMin;
  if (v > inMax) v = inMax;
  return static_cast<int16_t>(outMin + (v - inMin) * (outMax - outMin) / (inMax - inMin));
}

}  // namespace

void touchInputBegin() {
  // XPT2046_Touchscreen::begin() calls the global SPI.begin() internally
  // with no arguments (default hardware pins). Pre-configuring the bus
  // here with the touch controller's actual pins first makes that
  // internal call a no-op on the pins we actually want.
  SPI.begin(kTouchClkPin, kTouchMisoPin, kTouchMosiPin, -1);
  touch.begin();
  touch.setRotation(1);  // match tft.setRotation(1) in main.cpp
}

TouchPoint touchInputRead() {
  TouchPoint p;
  if (!touch.touched()) {
    return p;
  }

  TS_Point raw = touch.getPoint();
  // Which raw axis maps to which screen axis (and whether either needs
  // inverting) depends on the physical digitizer's wiring — verify on
  // real hardware and adjust if X/Y come out swapped or mirrored.
  p.x = mapRange(raw.x, kRawXMin, kRawXMax, 0, kScreenWidth - 1);
  p.y = mapRange(raw.y, kRawYMin, kRawYMax, 0, kScreenHeight - 1);
  p.pressed = true;
  return p;
}

#endif  // ARDUINO
