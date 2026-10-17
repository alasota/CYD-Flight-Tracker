#include "status_bar.h"

#include "lcars_theme.h"  // palette (pure section) + drawing helpers
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

std::string statusBarBlockLabel(int aircraftCount, bool hasData, Language lang) {
  if (!hasData) return "--";

  constexpr int kMaxShown = 99;
  const int n = aircraftCount < 0 ? 0 : aircraftCount;
  std::string number = (n > kMaxShown) ? std::to_string(kMaxShown) + "+" : std::to_string(n);

  // "99+" reads as "many" in both languages.
  const PluralForm form = (n > kMaxShown) ? PluralForm::Many : pluralForm(n, lang);
  const Str word = (form == PluralForm::One)   ? Str::FlightsOne
                   : (form == PluralForm::Few) ? Str::FlightsFew
                                               : Str::FlightsMany;
  return number + " " + tr(word, lang);
}

uint16_t statusBarBlockColor(OpenSkyHealth health) {
  switch (health) {
    case OpenSkyHealth::RateLimited:
      return LCARS_YELLOW;
    case OpenSkyHealth::AuthError:
    case OpenSkyHealth::NetworkError:
      return LCARS_MAGENTA;
    case OpenSkyHealth::Ok:
    default:
      return LCARS_ORANGE;
  }
}

#ifdef ARDUINO

namespace {
// Wide enough for the longest block label ("99+ FLIGHTS", 11 chars x 6px)
// in LCARS_FONT_SMALL plus padding, per CLAUDE.md "Screen chrome".
constexpr int16_t kNameBlockWidth = 80;
constexpr int16_t kNameBlockRadius = LCARS_HEADER_HEIGHT / 2;  // pill ends
constexpr int16_t kTextGap = 6;
constexpr int16_t kRightMargin = 4;
}  // namespace

void drawStatusBar(LGFX &gfx, int aircraftCount, bool hasData, OpenSkyHealth health,
                   std::time_t localEpoch, bool timeSynced, int16_t screenWidth) {
  // Own background — repaints the whole header without ghosting (also
  // behind the block's rounded corners).
  gfx.fillRect(0, 0, screenWidth, LCARS_HEADER_HEIGHT, LCARS_BLACK);

  // Left: aircraft-count block, coloured by data health.
  drawHeaderBlock(gfx, 0, 0, kNameBlockWidth, LCARS_HEADER_HEIGHT, kNameBlockRadius,
                  statusBarBlockColor(health), LCARS_BLACK,
                  statusBarBlockLabel(aircraftCount, hasData, i18nLanguage()).c_str());

  // Date (left-aligned, right after the block) + clock (right-aligned), cyan.
  gfx.setFont(LCARS_FONT_SMALL);  // slim 15px header
  gfx.setTextColor(LCARS_CYAN, LCARS_BLACK);

  int16_t midY = LCARS_HEADER_HEIGHT / 2;

  gfx.setTextDatum(middle_left);
  gfx.drawString(statusBarDate(localEpoch, timeSynced).c_str(),
                 static_cast<int16_t>(kNameBlockWidth + kTextGap), midY);

  std::string clock = statusBarClock(localEpoch, timeSynced);
  gfx.setTextDatum(middle_right);
  gfx.drawString(clock.c_str(), static_cast<int16_t>(screenWidth - kRightMargin), midY);
}

#endif  // ARDUINO
