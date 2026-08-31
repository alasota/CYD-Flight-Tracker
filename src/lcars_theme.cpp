#include "lcars_theme.h"

#include <cmath>

ElbowArcPoint elbowArcPoint(int16_t radius, float angle_deg) {
  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
  float rad = angle_deg * kDegToRad;

  ElbowArcPoint p;
  p.x = static_cast<int16_t>(lroundf(static_cast<float>(radius) * cosf(rad)));
  p.y = static_cast<int16_t>(lroundf(static_cast<float>(radius) * sinf(rad)));
  return p;
}

#ifdef ARDUINO

const lgfx::IFont *const LCARS_FONT_HEADING = &fonts::Font4;
const lgfx::IFont *const LCARS_FONT_BODY = &fonts::Font2;
const lgfx::IFont *const LCARS_FONT_NUMERIC = &fonts::Font7;

namespace {
constexpr int16_t kPanelCornerRadius = 8;
}  // namespace

void drawPanel(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  gfx.fillRoundRect(x, y, w, h, kPanelCornerRadius, color);
}

void drawElbow(LGFX &gfx, int16_t x, int16_t y, int16_t height, int16_t headerWidth,
               int16_t barWidth, uint16_t color) {
  // outerRadius = 2*barWidth gives the curve a chunky, typically-LCARS
  // proportion; innerRadius = barWidth keeps the bar thickness constant
  // all the way around the bend.
  int16_t outerRadius = barWidth * 2;
  int16_t cx = x + outerRadius;
  int16_t cy = y + outerRadius;

  // Concave corner joint (the "pipe bend"): a quarter annulus from 180°
  // (west — its outer/inner edges land exactly on the vertical bar's
  // top-left/top-right corners) to 270° (north — its outer/inner edges
  // land exactly on the header's top-left/bottom-left corners), per
  // LovyanGFX's fillArc angle convention (0=east, clockwise). See
  // elbowArcPoint() for the same math, exposed for testing.
  gfx.fillArc(cx, cy, barWidth, outerRadius, 180.0f, 270.0f, color);

  // Vertical bar: from just below the curve down to (x, y + height).
  gfx.fillRect(x, cy, barWidth, (y + height) - cy, color);

  // Horizontal header: from just right of the curve out to headerWidth.
  gfx.fillRect(cx, y, (x + headerWidth) - cx, barWidth, color);
}

void drawPillButton(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fillColor,
                     uint16_t textColor, const char *text) {
  int16_t radius = h / 2;
  gfx.fillRoundRect(x, y, w, h, radius, fillColor);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(textColor, fillColor);
  gfx.drawString(text, x + w / 2, y + h / 2);
}

#endif  // ARDUINO
