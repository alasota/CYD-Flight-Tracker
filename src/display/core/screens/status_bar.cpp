#include "display/core/screens/status_bar.h"

#include "display/core/theme/lcars_theme.h"  // palette roles, LCARS_FONT_SMALL_CHAR_PX
#include "display/core/screen_nav.h"  // kScreenFlights / kScreenFlight / kScreenRadar
#include "display/core/text/utf8.h"  // utf8Length()
#include "shared/clock.h"

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
    return "--.--.----";
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

uint16_t statusBarHealthColor(OpenSkyHealth health) {
  switch (health) {
    case OpenSkyHealth::RateLimited:
      return LCARS_YELLOW;
    case OpenSkyHealth::AuthError:
    case OpenSkyHealth::NetworkError:
      return THEME_ALERT;
    case OpenSkyHealth::Ok:
    default:
      return LCARS_GREEN;
  }
}

Str emptyStateText(OpenSkyHealth health, bool shortForm) {
  switch (health) {
    case OpenSkyHealth::RateLimited:
      return shortForm ? Str::FeedQuotaShort : Str::FeedQuotaExhausted;
    case OpenSkyHealth::AuthError:
      return shortForm ? Str::FeedErrorShort : Str::FeedAuthError;
    case OpenSkyHealth::NetworkError:
      return shortForm ? Str::FeedErrorShort : Str::FeedUnreachable;
    case OpenSkyHealth::Ok:
    default:
      return shortForm ? Str::NoFlight : Str::NoFlightsInRange;
  }
}

namespace {
constexpr int kMargin = 4;          // from the screen edges
// DejaVu12 (proportional) advance estimate for the layout maths: digits ~7px,
// "." ":" "-" "/" ~4px. The device draws with the real measured widths.
int clockFontWidthEstimate(const std::string &s) {
  int w = 0;
  for (char c : s) w += (c >= '0' && c <= '9') ? 7 : 4;
  return w;
}
}  // namespace

HeaderZones statusBarZones(int screenWidth, const std::string &appName,
                           const std::string &countLabel, const std::string &date,
                           const std::string &clock, bool badge) {
  HeaderZones z;
  z.name_x = kMargin;
  z.name_end = z.name_x + static_cast<int>(utf8Length(appName)) * LCARS_FONT_SMALL_CHAR_PX;
  if (badge) z.alert_x = z.name_end + kAlertBadgeGap;
  const int leftEnd = badge ? z.alert_x + kAlertBadgeDiameter : z.name_end;

  z.right_end = screenWidth - kMargin;
  const int clockW = clockFontWidthEstimate(clock);
  // Date in the clock font too (review #9: the dots of "18.10.2026" in the
  // 10px font looked like "+").
  const int dateW = clockFontWidthEstimate(date);
  z.right_start = z.right_end - clockW - kStatusBarDateClockGap - dateW;
  z.date_x = z.right_start;

  // Count centred in the free space between the name and the date (the
  // wide clock-font date left it glued to the date at screen centre).
  const int countW =
      kStatusBarDotDiameter + kStatusBarDotGap +
      static_cast<int>(utf8Length(countLabel)) * LCARS_FONT_SMALL_CHAR_PX;
  z.count_start = (leftEnd + z.right_start) / 2 - countW / 2;
  z.count_end = z.count_start + countW;
  return z;
}

HeaderZones statusBarZonesNow(int screenWidth, int aircraftCount, bool hasData, Language lang,
                              std::time_t localEpoch, bool timeSynced, bool badge) {
  return statusBarZones(screenWidth, kStatusBarAppName,
                        statusBarBlockLabel(aircraftCount, hasData, lang),
                        statusBarDate(localEpoch, timeSynced),
                        statusBarClock(localEpoch, timeSynced), badge);
}
