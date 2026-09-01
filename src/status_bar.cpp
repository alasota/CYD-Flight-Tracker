#include "status_bar.h"

#include "screen_nav.h"  // kScreenFlights / kScreenFlight / kScreenRadar
#include "time_sync.h"

std::string statusBarScreenName(int screenIndex) {
  switch (screenIndex) {
    case kScreenFlights:
      return "FLIGHTS";
    case kScreenFlight:
      return "FLIGHT";
    case kScreenRadar:
      return "RADAR";
    default:
      return "?";
  }
}

std::string statusBarStardate(std::time_t localEpoch, bool timeSynced) {
  if (!timeSynced) {
    return "STARDATE: ----.--";
  }
  return "STARDATE: " + computeStardate(localEpoch);
}

std::string statusBarClock(std::time_t localEpoch, bool timeSynced) {
  if (!timeSynced) {
    return "--:--";
  }
  return formatTime(localEpoch);
}

#ifdef ARDUINO

#include "lcars_theme.h"

namespace {
// Wide enough for the longest name ("FLIGHTS", 7 chars) in LCARS_FONT_BODY
// at this height, per CLAUDE.md "Screen chrome".
constexpr int16_t kNameBlockWidth = 78;
constexpr int16_t kNameBlockRadius = 6;
constexpr int16_t kTextGap = 6;
constexpr int16_t kRightMargin = 4;
}  // namespace

void drawStatusBar(LGFX &gfx, int screenIndex, std::time_t localEpoch, bool timeSynced,
                   int16_t screenWidth) {
  // Own background — safe to repaint every frame without ghosting.
  gfx.fillRect(0, 0, screenWidth, LCARS_HEADER_HEIGHT, LCARS_BLACK);

  // Left: orange name block.
  drawHeaderBlock(gfx, 0, 0, kNameBlockWidth, LCARS_HEADER_HEIGHT, kNameBlockRadius, LCARS_ORANGE,
                  LCARS_BLACK, statusBarScreenName(screenIndex).c_str());

  // Right of it: STARDATE (left-aligned) + clock (right-aligned), cyan.
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);

  int16_t midY = LCARS_HEADER_HEIGHT / 2;

  gfx.setTextDatum(middle_left);
  gfx.drawString(statusBarStardate(localEpoch, timeSynced).c_str(),
                 static_cast<int16_t>(kNameBlockWidth + kTextGap), midY);

  gfx.setTextDatum(middle_right);
  gfx.drawString(statusBarClock(localEpoch, timeSynced).c_str(),
                 static_cast<int16_t>(screenWidth - kRightMargin), midY);
}

#endif  // ARDUINO
