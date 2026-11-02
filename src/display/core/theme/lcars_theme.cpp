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
