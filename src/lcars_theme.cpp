#include "lcars_theme.h"

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

namespace {
// Beyond printable ASCII: ą ć ę ł ń ó ś ź ż Ą Ć Ę Ł Ń Ó Ś Ź Ż and ° ± × · – — … →.
// Keep in step with tools/subset_efont.py (test_fonts checks the fonts).
const uint16_t kFontExtraCodepoints[] = {
    0x0105, 0x0107, 0x0119, 0x0142, 0x0144, 0x00F3, 0x015B, 0x017A, 0x017C,
    0x0104, 0x0106, 0x0118, 0x0141, 0x0143, 0x00D3, 0x015A, 0x0179, 0x017B,
    0x00B0, 0x00B1, 0x00D7, 0x00B7, 0x2013, 0x2014, 0x2026, 0x2192};
}  // namespace

bool isFontCodepoint(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  for (uint16_t extra : kFontExtraCodepoints) {
    if (cp == extra) return true;
  }
  return false;
}

bool isFontSafeText(const std::string &text) {
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    uint32_t cp;
    size_t len;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      len = 3;
    } else {
      return false;  // 4-byte sequences / stray continuation bytes: never in the fonts
    }
    if (i + len > text.size()) return false;
    for (size_t k = 1; k < len; ++k) {
      const unsigned char cc = static_cast<unsigned char>(text[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!isFontCodepoint(cp)) return false;
    i += len;
  }
  return true;
}

bool fontHasGlyph(const uint8_t *font, uint16_t cp) {
  // Same search as LovyanGFX's U8g2font::getGlyph().
  const uint8_t *p = font + 23;
  if (cp <= 255) {
    if (cp >= 'a') {
      p += (font[19] << 8) | font[20];
    } else if (cp >= 'A') {
      p += (font[17] << 8) | font[18];
    }
    for (; p[1] != 0; p += p[1]) {
      if (p[0] == cp) return true;
    }
    return false;
  }
  p += (font[21] << 8) | font[22];
  const uint8_t *lut = p;
  uint16_t e;
  do {
    p += (lut[0] << 8) | lut[1];
    e = static_cast<uint16_t>((lut[2] << 8) | lut[3]);
    lut += 4;
  } while (e < cp);
  for (; (e = static_cast<uint16_t>((p[0] << 8) | p[1])) != 0; p += p[2]) {
    if (e == cp) return true;
  }
  return false;
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

#include "fonts/lcars_efont.h"  // subset efont 16 / 14 / 10
#include "screen_nav.h"  // kScreenCount, bottomNavBounds(), bottomNavSegment()

const lgfx::IFont *const LCARS_FONT_HEADING = &fonts::Font4;
// efont (U8g2-format, Unicode) instead of the built-in Font2 / Font0: same
// 16px / ~10px sizes, but it has the Polish letters (ąćęłńóśźż), so text
// no longer has to be ASCII-folded. Not LovyanGFX's fonts::efontJA_* (~1 MB
// of kanji together) but subsets of them with only the characters
// isFontSafeText() allows — same pixels and metrics, ~4 KB (src/fonts/,
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
