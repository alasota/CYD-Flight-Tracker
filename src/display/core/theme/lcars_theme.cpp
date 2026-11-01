#include "display/core/theme/lcars_theme.h"

#include <cmath>
#include <cstdio>

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

PhaseIconKind phaseIconKind(Phase phase, bool onGround) {
  if (onGround) return PhaseIconKind::GROUND;
  switch (phase) {
    case Phase::TAKEOFF:
      return PhaseIconKind::TAKEOFF;
    case Phase::LANDING:
      return PhaseIconKind::LANDING;
    case Phase::OVERFLIGHT:
      return PhaseIconKind::OVERFLIGHT;
    case Phase::NONE:
    default:
      return PhaseIconKind::NONE;
  }
}

namespace {
// 16 x 12 pixel art, see phaseIconRows() for the legend. Side view, nose
// right; the runway strip sits under the tail (takeoff: leaving it) or under
// the nose (landing: heading for it).
const char *const kIconTakeoff[kPhaseIconH] = {
    "................",
    "................",
    "................",
    "#............#k#",
    "##........######",
    "##.....######...",
    "##..#########...",
    "##########......",
    ".######dd.......",
    ".###.ddd........",
    "................",
    "ggggggggg.......",
};
const char *const kIconLanding[kPhaseIconH] = {
    "##..............",
    "##..............",
    "##..............",
    "####............",
    "#######.........",
    ".#########......",
    "....#########...",
    ".....dd#######k.",
    "......ddd.######",
    ".............###",
    "................",
    ".......ggggggggg",
};
const char *const kIconOverflight[kPhaseIconH] = {
    "................",
    "................",
    "................",
    "#...............",
    "##..............",
    "###.............",
    "############k#..",
    "###k#k#k#k######",
    ".#############..",
    ".....dddd.......",
    "......dd........",
    "................",
};
// Top view, nose right: swept wings with engines, tailplane.
const char *const kIconGround[kPhaseIconH] = {
    "......##........",
    ".......##.......",
    "........##dd....",
    "##.......##.....",
    ".##......###....",
    ".##############.",
    ".##############.",
    ".##......###....",
    "##.......##.....",
    "........##dd....",
    ".......##.......",
    "......##........",
};
}  // namespace

const char *const *phaseIconRows(PhaseIconKind kind) {
  switch (kind) {
    case PhaseIconKind::TAKEOFF:
      return kIconTakeoff;
    case PhaseIconKind::LANDING:
      return kIconLanding;
    case PhaseIconKind::OVERFLIGHT:
      return kIconOverflight;
    case PhaseIconKind::GROUND:
      return kIconGround;
    case PhaseIconKind::NONE:
    default:
      return nullptr;
  }
}

uint16_t phaseIconColor(PhaseIconKind kind) {
  switch (kind) {
    case PhaseIconKind::TAKEOFF:
      return phaseColor(Phase::TAKEOFF);
    case PhaseIconKind::LANDING:
      return phaseColor(Phase::LANDING);
    case PhaseIconKind::OVERFLIGHT:
      return phaseColor(Phase::OVERFLIGHT);
    case PhaseIconKind::GROUND:
    case PhaseIconKind::NONE:
    default:
      return THEME_CONTENT_MUTED;
  }
}

uint16_t shadeRgb565(uint16_t color, int num, int den) {
  const int r = ((color >> 11) & 0x1F) * num / den;
  const int g = ((color >> 5) & 0x3F) * num / den;
  const int b = (color & 0x1F) * num / den;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

bool phaseIconPixelColor(PhaseIconKind kind, char pixel, uint16_t *color) {
  switch (pixel) {
    case '#':
      *color = phaseIconColor(kind);
      return true;
    case 'd':
      *color = shadeRgb565(phaseIconColor(kind), 11, 20);  // ~55%
      return true;
    case 'k':
      *color = LCARS_BLACK;
      return true;
    case 'g':
      *color = LCARS_RUNWAY_GREY;
      return true;
    default:
      return false;
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

uint16_t tableRowColor(int rowIndex, bool onGround) {
  return onGround ? THEME_CONTENT_MUTED : tableRowTextColor(rowIndex);
}

uint16_t tableRowTextColor(int rowIndex) {
  return (rowIndex % 2 == 0) ? THEME_CONTENT_PRIMARY : THEME_CONTENT_SECONDARY;
}

#ifdef ARDUINO

#include "display/core/theme/fonts/lcars_efont.h"  // subset efont 16 / 14 / 10
#include "display/core/screen_nav.h"  // kScreenCount, bottomNavBounds(), bottomNavSegment()

const lgfx::IFont *const LCARS_FONT_HEADING = &fonts::Font4;
// efont (U8g2-format, Unicode) instead of the built-in Font2 / Font0: same
// 16px / ~10px sizes, but it has the Polish letters (ąćęłńóśźż), so text
// no longer has to be ASCII-folded. Not LovyanGFX's fonts::efontJA_* (~1 MB
// of kanji together) but subsets of them with only the characters
// isFontSafeText() allows — same pixels and metrics, ~4 KB (theme/fonts/,
// tools/subset_efont.py).
namespace {
const lgfx::U8g2font kEfont16(lcars_efont_16);
const lgfx::U8g2font kEfont14(lcars_efont_14);
const lgfx::U8g2font kEfont10(lcars_efont_10);
}  // namespace
const lgfx::IFont *const LCARS_FONT_BODY = &kEfont16;
const lgfx::IFont *const LCARS_FONT_SMALL = &kEfont10;
// A touch larger than the 10px header text but still fits the 15px header
// (13px line); a few KB, ASCII only — fine for "08:46".
const lgfx::IFont *const LCARS_FONT_CLOCK = &fonts::DejaVu12;
const lgfx::IFont *const LCARS_FONT_TABLE = &kEfont14;

TextMeasure gfxTextMeasure(lgfx::LovyanGFX &gfx) {
  return [&gfx](const std::string &s) { return static_cast<int>(gfx.textWidth(s.c_str())); };
}

void drawPhaseIcon(LGFX &gfx, PhaseIconKind kind, int16_t cx, int16_t cy) {
  const char *const *rows = phaseIconRows(kind);
  if (rows == nullptr) return;
  const int16_t x0 = static_cast<int16_t>(cx - kPhaseIconW / 2);
  const int16_t y0 = static_cast<int16_t>(cy - kPhaseIconH / 2);
  gfx.startWrite();
  for (int16_t y = 0; y < kPhaseIconH; ++y) {
    const char *row = rows[y];
    int16_t x = 0;
    while (x < kPhaseIconW) {
      // One line per run of the same character.
      int16_t end = x;
      while (end < kPhaseIconW && row[end] == row[x]) ++end;
      uint16_t color;
      if (phaseIconPixelColor(kind, row[x], &color)) {
        gfx.drawFastHLine(static_cast<int16_t>(x0 + x), static_cast<int16_t>(y0 + y),
                          static_cast<int16_t>(end - x), color);
      }
      x = end;
    }
  }
  gfx.endWrite();
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

namespace {
// A tab: rounded bottom corners, square top. LovyanGFX can't round single
// corners, so it's a rounded rect with its top corner rows squared off.
void drawNavTabShape(LGFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  int16_t r = kNavTabCornerRadius;
  if (r > h / 2) r = static_cast<int16_t>(h / 2);
  gfx.fillRoundRect(x, y, w, h, r, color);
  gfx.fillRect(x, y, w, r, color);
}
}  // namespace

void drawBottomNav(LGFX &gfx, int activeIndex, int16_t screenWidth, int16_t screenHeight,
                   const char *(*labelFor)(int screenIndex, Language lang)) {
  // Repaint the whole strip first so a screen change doesn't leave the
  // previous active tab behind.
  Rect bar = bottomNavBounds(screenWidth, screenHeight);
  gfx.fillRect(bar.x, bar.y, bar.w, bar.h, LCARS_BLACK);

  // The rule the tabs hang from; the active tab (same colour) merges into it.
  Rect rule = navTabRule(screenWidth, screenHeight);
  gfx.fillRect(rule.x, rule.y, rule.w, rule.h, THEME_CHROME_ACCENT);

  gfx.setFont(LCARS_FONT_BODY);  // big, readable labels
  gfx.setTextDatum(middle_center);

  // Inactive tabs first, active one last so its overlap sits on top.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < kScreenCount; ++i) {
      const bool active = (i == activeIndex);
      if (active != (pass == 1)) continue;
      Rect t = navTabRect(i, active, screenWidth, screenHeight);
      uint16_t fill;
      uint16_t text;
      if (active) {
        fill = THEME_CHROME_ACCENT;
        text = THEME_ON_FILL;
        drawNavTabShape(gfx, t.x, t.y, t.w, t.h, fill);
      } else {
        fill = THEME_CHROME_RECESSED;
        text = THEME_CHROME_INACTIVE;
        // 1px outline: outer shape in lavender, inner shape (inset on the
        // sides and bottom; the top edge stays outlined too) in the fill.
        drawNavTabShape(gfx, t.x, t.y, t.w, t.h, THEME_CHROME_INACTIVE);
        drawNavTabShape(gfx, static_cast<int16_t>(t.x + 1), static_cast<int16_t>(t.y + 1),
                        static_cast<int16_t>(t.w - 2), static_cast<int16_t>(t.h - 2), fill);
      }
      // Centre the label in the part of the tab below the rule.
      const int16_t textTop = active ? static_cast<int16_t>(t.y + kNavTabRulePx) : t.y;
      gfx.setTextColor(text, fill);
      gfx.drawString(labelFor(i, i18nLanguage()), static_cast<int16_t>(t.x + t.w / 2),
                     static_cast<int16_t>(textTop + (t.y + t.h - textTop) / 2));
    }
  }
}

#endif  // ARDUINO
