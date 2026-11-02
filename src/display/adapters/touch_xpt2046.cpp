#include "display/adapters/touch_xpt2046.h"

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
