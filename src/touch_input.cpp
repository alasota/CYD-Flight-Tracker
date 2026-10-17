#include "touch_input.h"

#include "touch_calibration.h"

bool hitTest(int16_t touchX, int16_t touchY, const Rect &bounds) {
  return touchX >= bounds.x && touchX <= bounds.x + bounds.w && touchY >= bounds.y &&
         touchY <= bounds.y + bounds.h;
}

int16_t mapTouchAxis(int32_t raw, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax) {
  if (inMin == inMax) {
    return static_cast<int16_t>(outMin);  // degenerate calibration range — don't divide by zero
  }
  if (raw < inMin) raw = inMin;
  if (raw > inMax) raw = inMax;
  return static_cast<int16_t>(outMin + (raw - inMin) * (outMax - outMin) / (inMax - inMin));
}

namespace {
constexpr int16_t kScreenWidth = 320;   // landscape
constexpr int16_t kScreenHeight = 240;
}  // namespace

TouchXY mapRawTouch(int32_t rawX, int32_t rawY) {
  // Mirrors Panel_Device::convertRawXY for rotation 1: the calibration
  // maps raw X onto the panel's short side and raw Y onto its long side,
  // then rotation 1 swaps them and flips the vertical.
  TouchXY p;
  p.x = mapTouchAxis(rawY, kTouchRawYMin, kTouchRawYMax, 0, kScreenWidth - 1);
  p.y = mapTouchAxis(rawX, kTouchRawXMin, kTouchRawXMax, kScreenHeight - 1, 0);
  return p;
}

#ifdef ARDUINO

TouchPoint touchInputRead(LGFX &gfx) {
  TouchPoint p;
  lgfx::touch_point_t raw;
  if (gfx.getTouchRaw(&raw, 1) == 0) {
    return p;
  }

  int32_t x = 0;
  int32_t y = 0;
  if (!gfx.getTouch(&x, &y)) {
    return p;  // released between the two reads
  }
  p.x = static_cast<int16_t>(x);
  p.y = static_cast<int16_t>(y);
  p.raw_x = static_cast<int16_t>(raw.x);
  p.raw_y = static_cast<int16_t>(raw.y);
  p.pressed = true;
  return p;
}

#endif  // ARDUINO
