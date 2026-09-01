#include "lcars_theme.h"

#include <cmath>
#include <cstdio>

ElbowArcPoint elbowArcPoint(int16_t radius, float angle_deg) {
  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
  float rad = angle_deg * kDegToRad;

  ElbowArcPoint p;
  p.x = static_cast<int16_t>(lroundf(static_cast<float>(radius) * cosf(rad)));
  p.y = static_cast<int16_t>(lroundf(static_cast<float>(radius) * sinf(rad)));
  return p;
}

namespace {
constexpr int16_t kToggleButtonWidth = 54;
constexpr int16_t kToggleButtonHeight = 22;
constexpr int16_t kToggleButtonMargin = 4;
}  // namespace

Rect viewToggleButtonBounds(int16_t screenWidth, int16_t screenHeight) {
  (void)screenHeight;  // button is anchored to the top edge, not the bottom

  Rect r;
  r.w = kToggleButtonWidth;
  r.h = kToggleButtonHeight;
  r.x = static_cast<int16_t>(screenWidth - kToggleButtonWidth - kToggleButtonMargin);
  r.y = kToggleButtonMargin;
  return r;
}

char phaseIcon(Phase phase) {
  switch (phase) {
    case Phase::TAKEOFF:
      return '^';
    case Phase::LANDING:
      return 'v';
    case Phase::OVERFLIGHT:
      return '>';
    case Phase::NONE:
    default:
      return '-';
  }
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

void drawViewToggleButton(LGFX &gfx, int16_t screenWidth, int16_t screenHeight) {
  Rect b = viewToggleButtonBounds(screenWidth, screenHeight);
  drawPillButton(gfx, b.x, b.y, b.w, b.h, LCARS_BLUE_VIOLET, LCARS_BLACK, "VIEW");
}

void drawElbowFrame(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t cornerRadius, int16_t thickness, uint16_t color) {
  int16_t r = cornerRadius;
  int16_t t = thickness;
  if (r < t) r = t;  // a corner smaller than the border can't be swept

  // Four straight edges. The top and left ones start past the swept
  // corner; the bottom and right ones run the full span (square corners).
  gfx.fillRect(x + r, y, w - r, t, color);            // top
  gfx.fillRect(x, y + h - t, w, t, color);            // bottom
  gfx.fillRect(x, y + r, t, h - r, color);            // left
  gfx.fillRect(x + w - t, y, t, h, color);            // right

  // Swept top-left corner: quarter annulus centered at (x+r, y+r), outer
  // radius r, inner radius r-t, swept 180deg (west — meets the left edge
  // at (x, y+r)) to 270deg (north — meets the top edge at (x+r, y)). See
  // elbowArcPoint() for the same math, exposed for tests.
  gfx.fillArc(x + r, y + r, r, static_cast<int16_t>(r - t), 180.0f, 270.0f, color);
}

void drawVerticalDivider(LGFX &gfx, int16_t x, int16_t y, int16_t h, int16_t thickness,
                         uint16_t color) {
  gfx.fillRect(x, y, thickness, h, color);
}

void drawRadarRing(LGFX &gfx, int16_t centerX, int16_t centerY, int16_t radiusPx,
                   int distanceKm, uint16_t color) {
  gfx.drawCircle(centerX, centerY, radiusPx, color);

  char buf[12];
  std::snprintf(buf, sizeof(buf), "%dkm", distanceKm);
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(top_center);
  gfx.setTextColor(color, LCARS_BLACK);
  // Just inside the ring at its 12-o'clock point, so the text clears both
  // the outer circle and the next ring out.
  gfx.drawString(buf, centerX, static_cast<int16_t>(centerY - radiusPx + 1));
}

void drawHeaderBlock(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t cornerRadius,
                     uint16_t fillColor, uint16_t textColor, const char *label) {
  gfx.fillRoundRect(x, y, w, h, cornerRadius, fillColor);

  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(textColor, fillColor);
  gfx.drawString(label, static_cast<int16_t>(x + w / 2), static_cast<int16_t>(y + h / 2));
}

#endif  // ARDUINO
