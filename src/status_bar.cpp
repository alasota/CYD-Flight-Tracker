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

const char *statusBarHealthTag(OpenSkyHealth health) {
  switch (health) {
    case OpenSkyHealth::RateLimited:
      return "RATE";
    case OpenSkyHealth::AuthError:
      return "AUTH";
    case OpenSkyHealth::NetworkError:
      return "NET";
    case OpenSkyHealth::Ok:
    default:
      return "";
  }
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
                   OpenSkyHealth health, int16_t screenWidth) {
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

  std::string clock = statusBarClock(localEpoch, timeSynced);
  gfx.setTextDatum(middle_right);
  gfx.drawString(clock.c_str(), static_cast<int16_t>(screenWidth - kRightMargin), midY);

  // Data-feed health tag, just left of the clock, in orange so a stale
  // aircraft list is visibly flagged (review notes 1.2/1.3/1.4).
  const char *tag = statusBarHealthTag(health);
  if (tag[0] != '\0') {
    int16_t clockW = static_cast<int16_t>(gfx.textWidth(clock.c_str()));
    gfx.setTextColor(LCARS_ORANGE, LCARS_BLACK);
    gfx.drawString(tag, static_cast<int16_t>(screenWidth - kRightMargin - clockW - kTextGap), midY);
    gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
  }
}

#endif  // ARDUINO
