#include "display/adapters/touch_xpt2046.h"

#include <Arduino.h>

#include "display/core/screen_nav.h"  // navHitTest()

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

void touchLogTap(LGFX &gfx, const TouchPoint &t) {
  const NavHit hit = navHitTest(t.x, t.y, static_cast<int16_t>(gfx.width()),
                                static_cast<int16_t>(gfx.height()));
  Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d) nav=%d\n", t.raw_x, t.raw_y, t.x, t.y,
                static_cast<int>(hit.action));
}
