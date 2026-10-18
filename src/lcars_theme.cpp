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

uint16_t phaseColor(Phase phase) {
  switch (phase) {
    case Phase::TAKEOFF:
      return LCARS_GREEN;
    case Phase::LANDING:
      return LCARS_PEACH;
    case Phase::OVERFLIGHT:
      return LCARS_SKY;
    case Phase::NONE:
    default:
      return THEME_CONTENT_MUTED;
  }
}

namespace {
PhaseTriangle makeTriangle(int x0, int y0, int x1, int y1, int x2, int y2) {
  PhaseTriangle t;  // (no brace-init: the ESP32 toolchain builds as C++11)
  t.visible = true;
  t.x0 = static_cast<int16_t>(x0);
  t.y0 = static_cast<int16_t>(y0);
  t.x1 = static_cast<int16_t>(x1);
  t.y1 = static_cast<int16_t>(y1);
  t.x2 = static_cast<int16_t>(x2);
  t.y2 = static_cast<int16_t>(y2);
  return t;
}
}  // namespace

PhaseTriangle phaseTriangle(Phase phase, int16_t cx, int16_t cy, int16_t size) {
  const int h = size / 2;
  switch (phase) {
    case Phase::TAKEOFF:  // ▲ — apex on top
      return makeTriangle(cx, cy - h, cx - h, cy + h, cx + h, cy + h);
    case Phase::LANDING:  // ▼ — apex at the bottom
      return makeTriangle(cx, cy + h, cx - h, cy - h, cx + h, cy - h);
    case Phase::OVERFLIGHT:  // ▶ — tip on the right
      return makeTriangle(cx + h, cy, cx - h, cy - h, cx - h, cy + h);
    case Phase::NONE:
    default:
      return PhaseTriangle();  // no icon
  }
}

ArrowShape directionArrow(float bearingDeg, int16_t cx, int16_t cy, int16_t size) {
  // Unit vector along the bearing (screen y grows down) and its normal.
  const float rad = bearingDeg * 3.14159265f / 180.0f;
  const float dx = std::sin(rad);
  const float dy = -std::cos(rad);
  const float r = static_cast<float>(size / 2);  // tip and tail sit on this circle
  const float headLen = 0.45f * static_cast<float>(size);
  const float headHalf = 0.3f * static_cast<float>(size);
  auto px = [&](float along, float across) {
    return static_cast<int>(std::lround(cx + along * dx - across * dy));
  };
  auto py = [&](float along, float across) {
    return static_cast<int>(std::lround(cy + along * dy + across * dx));
  };

  ArrowShape a;
  const float base = r - headLen;  // where the head's flat side crosses the axis
  a.head = makeTriangle(px(r, 0), py(r, 0), px(base, headHalf), py(base, headHalf),
                        px(base, -headHalf), py(base, -headHalf));
  a.stem_x0 = static_cast<int16_t>(px(-r, 0));
  a.stem_y0 = static_cast<int16_t>(py(-r, 0));
  a.stem_x1 = static_cast<int16_t>(px(base, 0));
  a.stem_y1 = static_cast<int16_t>(py(base, 0));
  return a;
}

const char *compassPoint(float bearingDeg) {
  static const char *const kPoints[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  float b = std::fmod(bearingDeg, 360.0f);
  if (b < 0.0f) b += 360.0f;
  const int sector = static_cast<int>((b + 22.5f) / 45.0f) % 8;
  return kPoints[sector];
}

uint16_t tableRowColor(int rowIndex, bool onGround) {
  return onGround ? THEME_CONTENT_MUTED : tableRowTextColor(rowIndex);
}

uint16_t tableRowTextColor(int rowIndex) {
  return (rowIndex % 2 == 0) ? THEME_CONTENT_PRIMARY : THEME_CONTENT_SECONDARY;
}

namespace {
// True if byte `i` of `s` starts a character (i.e. isn't a UTF-8
// continuation byte 10xxxxxx) — the only places a string may be cut.
bool isCharBoundary(const std::string &s, size_t i) {
  return i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
}
}  // namespace

size_t utf8Length(const std::string &utf8) {
  size_t n = 0;
  for (size_t i = 0; i < utf8.size(); ++i) {
    if (isCharBoundary(utf8, i)) ++n;
  }
  return n;
}

bool isFontSafeText(const std::string &text) {
  // UTF-8 encodings of ą ć ę ł ń ó ś ź ż Ą Ć Ę Ł Ń Ó Ś Ź Ż.
  static const char *const kPolish[] = {
      "\xC4\x85", "\xC4\x87", "\xC4\x99", "\xC5\x82", "\xC5\x84", "\xC3\xB3",
      "\xC5\x9B", "\xC5\xBA", "\xC5\xBC", "\xC4\x84", "\xC4\x86", "\xC4\x98",
      "\xC5\x81", "\xC5\x83", "\xC3\x93", "\xC5\x9A", "\xC5\xB9", "\xC5\xBB"};
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c >= 0x20 && c <= 0x7E) {
      ++i;
      continue;
    }
    if (i + 1 >= text.size()) return false;
    bool polish = false;
    for (const char *p : kPolish) {
      if (text[i] == p[0] && text[i + 1] == p[1]) polish = true;
    }
    if (!polish) return false;
    i += 2;
  }
  return true;
}

std::string fitToWidth(const std::string &text, int maxPx, const TextMeasure &measure) {
  if (maxPx <= 0) return std::string();
  if (measure(text) <= maxPx) return text;

  // Longest head + "..." that fits (head must keep at least one char).
  for (size_t n = text.size() - 1; n >= 1; --n) {
    if (!isCharBoundary(text, n)) continue;  // never split a UTF-8 letter
    std::string head = text.substr(0, n);
    while (!head.empty() && head.back() == ' ') head.pop_back();
    if (head.empty()) continue;
    std::string candidate = head + "...";
    if (measure(candidate) <= maxPx) return candidate;
  }

  // Too tight for an ellipsis — longest plain head that fits.
  for (size_t n = text.size() - 1; n >= 1; --n) {
    if (!isCharBoundary(text, n)) continue;
    std::string head = text.substr(0, n);
    if (measure(head) <= maxPx) return head;
  }
  return std::string();
}

#ifdef ARDUINO

#include "screen_nav.h"  // kScreenCount, bottomNavBounds(), bottomNavSegment()

const lgfx::IFont *const LCARS_FONT_HEADING = &fonts::Font4;
// efont (U8g2-format, Unicode) instead of the built-in Font2 / Font0: same
// 16px / ~10px sizes, but it has the Polish letters (ąćęłńóśźż), so text
// no longer has to be ASCII-folded. ~470 KB + ~225 KB of flash — fits only
// with the huge_app partition table (platformio.ini).
const lgfx::IFont *const LCARS_FONT_BODY = &fonts::efontJA_16;
const lgfx::IFont *const LCARS_FONT_NUMERIC = &fonts::Font7;
const lgfx::IFont *const LCARS_FONT_SMALL = &fonts::efontJA_10;
// A touch larger than the 10px header text but still fits the 15px header
// (13px line); a few KB, ASCII only — fine for "08:46".
const lgfx::IFont *const LCARS_FONT_CLOCK = &fonts::DejaVu12;
const lgfx::IFont *const LCARS_FONT_TABLE = &fonts::efontJA_14;

TextMeasure gfxTextMeasure(lgfx::LovyanGFX &gfx) {
  return [&gfx](const std::string &s) { return static_cast<int>(gfx.textWidth(s.c_str())); };
}

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

void drawPhaseIcon(LGFX &gfx, Phase phase, int16_t cx, int16_t cy, int16_t size) {
  const PhaseTriangle t = phaseTriangle(phase, cx, cy, size);
  if (!t.visible) return;
  gfx.fillTriangle(t.x0, t.y0, t.x1, t.y1, t.x2, t.y2, phaseColor(phase));
}
void drawDirectionArrow(LGFX &gfx, float bearingDeg, int16_t cx, int16_t cy, int16_t size,
                        uint16_t color) {
  const ArrowShape a = directionArrow(bearingDeg, cx, cy, size);
  gfx.drawWideLine(a.stem_x0, a.stem_y0, a.stem_x1, a.stem_y1, 1.0f, color);  // radius 1 = ~2px stem
  gfx.fillTriangle(a.head.x0, a.head.y0, a.head.x1, a.head.y1, a.head.x2, a.head.y2, color);
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

void drawHeaderBlock(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t cornerRadius,
                     uint16_t fillColor, uint16_t textColor, const char *label) {
  gfx.fillRoundRect(x, y, w, h, cornerRadius, fillColor);

  gfx.setFont(LCARS_FONT_SMALL);  // slim 15px header
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(textColor, fillColor);
  gfx.drawString(label, static_cast<int16_t>(x + w / 2), static_cast<int16_t>(y + h / 2));
}

void drawBottomNav(LGFX &gfx, int activeIndex, int16_t screenWidth, int16_t screenHeight,
                   const char *(*labelFor)(int screenIndex, Language lang)) {
  // Repaint the whole strip first so a screen change doesn't leave the
  // previous active button behind.
  Rect bar = bottomNavBounds(screenWidth, screenHeight);
  gfx.fillRect(bar.x, bar.y, bar.w, bar.h, LCARS_BLACK);

  constexpr int16_t kInsetX = 3;  // gap between neighbouring buttons
  constexpr int16_t kInsetY = 2;

  gfx.setFont(LCARS_FONT_BODY);  // big, readable labels on the 25px buttons
  gfx.setTextDatum(middle_center);

  for (int i = 0; i < kScreenCount; ++i) {
    Rect seg = bottomNavSegment(i, screenWidth, screenHeight);
    const bool active = (i == activeIndex);

    // A pill button filling the segment (minus small insets).
    const int16_t px = static_cast<int16_t>(seg.x + kInsetX);
    const int16_t py = static_cast<int16_t>(seg.y + kInsetY);
    const int16_t pw = static_cast<int16_t>(seg.w - 2 * kInsetX);
    const int16_t ph = static_cast<int16_t>(seg.h - 2 * kInsetY);
    const int16_t r = static_cast<int16_t>(ph / 2);

    const uint16_t fill = active ? THEME_CHROME_ACCENT : THEME_CHROME_INACTIVE;
    const uint16_t text = THEME_ON_FILL;
    gfx.fillRoundRect(px, py, pw, ph, r, fill);

    gfx.setTextColor(text, fill);
    gfx.drawString(labelFor(i, i18nLanguage()), static_cast<int16_t>(px + pw / 2),
                   static_cast<int16_t>(py + ph / 2));
  }
}

#endif  // ARDUINO
