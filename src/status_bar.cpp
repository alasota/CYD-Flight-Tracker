#include "status_bar.h"

#include "screen_nav.h"  // kScreenFlights / kScreenFlight / kScreenRadar
#include "time_sync.h"

const char *statusBarScreenName(int screenIndex, Language lang) {
  switch (screenIndex) {
    case kScreenFlights:
      return tr(Str::ScreenFlights, lang);
    case kScreenFlight:
      return tr(Str::ScreenNow, lang);  // Screen 2 is displayed as NOW / TERAZ
    case kScreenRadar:
      return tr(Str::ScreenRadar, lang);
    default:
      return "?";
  }
}

std::string statusBarDate(std::time_t localEpoch, bool timeSynced) {
  if (!timeSynced) {
    return "--/--/----";
  }
  return formatDate(localEpoch);
}

std::string statusBarClock(std::time_t localEpoch, bool timeSynced) {
  if (!timeSynced) {
    return "--:--";
  }
  return formatTime(localEpoch);
}

const char *statusBarHealthTag(OpenSkyHealth health, Language lang) {
  switch (health) {
    case OpenSkyHealth::RateLimited:
      return tr(Str::HealthRateLimited, lang);
    case OpenSkyHealth::AuthError:
      return tr(Str::HealthAuthError, lang);
    case OpenSkyHealth::NetworkError:
      return tr(Str::HealthNetworkError, lang);
    case OpenSkyHealth::Ok:
    default:
      return "";
  }
}

#ifdef ARDUINO

#include "lcars_theme.h"

namespace {
// Wide enough for the longest name in any language ("FLIGHTS", 7 chars) in LCARS_FONT_BODY
// at this height, per CLAUDE.md "Screen chrome".
constexpr int16_t kNameBlockWidth = 78;
constexpr int16_t kNameBlockRadius = 6;
constexpr int16_t kTextGap = 6;
constexpr int16_t kRightMargin = 4;
}  // namespace

void drawStatusBar(LGFX &gfx, int screenIndex, std::time_t localEpoch, bool timeSynced,
                   OpenSkyHealth health, int16_t screenWidth) {
  // Left: orange name block (on its own black backing, so the rounded
  // corners don't keep stale pixels).
  gfx.fillRect(0, 0, kNameBlockWidth, LCARS_HEADER_HEIGHT, LCARS_BLACK);
  drawHeaderBlock(gfx, 0, 0, kNameBlockWidth, LCARS_HEADER_HEIGHT, kNameBlockRadius, LCARS_ORANGE,
                  LCARS_BLACK, statusBarScreenName(screenIndex, i18nLanguage()));

  drawStatusBarText(gfx, localEpoch, timeSynced, health, screenWidth);
}

void drawStatusBarText(LGFX &gfx, std::time_t localEpoch, bool timeSynced, OpenSkyHealth health,
                       int16_t screenWidth) {
  // Own background (text area only) — safe to repaint without ghosting
  // and without touching the name block.
  gfx.fillRect(kNameBlockWidth, 0, static_cast<int16_t>(screenWidth - kNameBlockWidth),
               LCARS_HEADER_HEIGHT, LCARS_BLACK);

  // Date (left-aligned, right after the block) + clock (right-aligned), cyan.
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);

  int16_t midY = LCARS_HEADER_HEIGHT / 2;

  gfx.setTextDatum(middle_left);
  gfx.drawString(statusBarDate(localEpoch, timeSynced).c_str(),
                 static_cast<int16_t>(kNameBlockWidth + kTextGap), midY);

  std::string clock = statusBarClock(localEpoch, timeSynced);
  gfx.setTextDatum(middle_right);
  gfx.drawString(clock.c_str(), static_cast<int16_t>(screenWidth - kRightMargin), midY);

  // Data-feed health tag, just left of the clock, in orange so a stale
  // aircraft list is visibly flagged (review notes 1.2/1.3/1.4).
  const char *tag = statusBarHealthTag(health, i18nLanguage());
  if (tag[0] != '\0') {
    int16_t clockW = static_cast<int16_t>(gfx.textWidth(clock.c_str()));
    gfx.setTextColor(LCARS_ORANGE, LCARS_BLACK);
    gfx.drawString(tag, static_cast<int16_t>(screenWidth - kRightMargin - clockW - kTextGap), midY);
    gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);
  }
}

#endif  // ARDUINO
