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
constexpr int kDotDiameter = 6;     // health dot
constexpr int kDotGap = 4;          // dot -> count label
constexpr int kDateClockGap = 8;    // date -> clock
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
                           const std::string &clock) {
  HeaderZones z;
  z.name_x = kMargin;
  z.name_end = z.name_x + static_cast<int>(utf8Length(appName)) * LCARS_FONT_SMALL_CHAR_PX;

  z.right_end = screenWidth - kMargin;
  const int clockW = clockFontWidthEstimate(clock);
  // Date in the clock font too (review #9: the dots of "18.10.2026" in the
  // 10px font looked like "+").
  const int dateW = clockFontWidthEstimate(date);
  z.right_start = z.right_end - clockW - kDateClockGap - dateW;
  z.date_x = z.right_start;

  // Count centred in the free space between the name and the date (the
  // wide clock-font date left it glued to the date at screen centre).
  const int countW =
      kDotDiameter + kDotGap + static_cast<int>(utf8Length(countLabel)) * LCARS_FONT_SMALL_CHAR_PX;
  z.count_start = (z.name_end + z.right_start) / 2 - countW / 2;
  z.count_end = z.count_start + countW;
  return z;
}

#ifdef ARDUINO

void drawStatusBar(LGFX &gfx, int aircraftCount, bool hasData, OpenSkyHealth health,
                   std::time_t localEpoch, bool timeSynced, int16_t screenWidth) {
  // Black strip + a 1px lilac rule along its bottom edge — no filled block,
  // so nothing up here looks like the (peach) active nav button.
  gfx.fillRect(0, 0, screenWidth, LCARS_HEADER_HEIGHT, LCARS_BLACK);
  gfx.drawFastHLine(0, static_cast<int16_t>(LCARS_HEADER_HEIGHT - 1), screenWidth,
                    THEME_TABLE_HEADER);

  const std::string count = statusBarBlockLabel(aircraftCount, hasData, i18nLanguage());
  const std::string date = statusBarDate(localEpoch, timeSynced);
  const std::string clock = statusBarClock(localEpoch, timeSynced);
  const HeaderZones z = statusBarZones(screenWidth, kStatusBarAppName, count, date, clock);
  const int16_t midY = static_cast<int16_t>((LCARS_HEADER_HEIGHT - 1) / 2);  // above the rule

  gfx.setTextDatum(middle_left);

  // Same arrangement as statusBarZones(), but placed with the real rendered
  // widths (DejaVu12 is proportional — the estimate let the date run into the
  // clock and crowd the count).

  // Left: app name, small and dim.
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextColor(THEME_CHROME_DIM, LCARS_BLACK);
  gfx.drawString(kStatusBarAppName, static_cast<int16_t>(z.name_x), midY);
  const int nameEnd = z.name_x + static_cast<int>(gfx.textWidth(kStatusBarAppName));
  const int countW = kDotDiameter + kDotGap + static_cast<int>(gfx.textWidth(count.c_str()));

  // Right: date + clock, both in the clock font (crisp dots — review #9).
  // The date stays light peach; the time is white so it still reads first.
  gfx.setFont(LCARS_FONT_CLOCK);
  const int clockW = static_cast<int>(gfx.textWidth(clock.c_str()));
  const int dateW = static_cast<int>(gfx.textWidth(date.c_str()));
  const int dateX = z.right_end - clockW - kDateClockGap - dateW;
  gfx.setTextColor(THEME_CHROME_TEXT, LCARS_BLACK);
  gfx.drawString(date.c_str(), static_cast<int16_t>(dateX), midY);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.setTextDatum(middle_right);
  gfx.drawString(clock.c_str(), static_cast<int16_t>(z.right_end), midY);

  // Centre: health dot + flight count, centred in the gap between the two.
  const int countX = (nameEnd + dateX) / 2 - countW / 2;
  gfx.fillCircle(static_cast<int16_t>(countX + kDotDiameter / 2), midY, kDotDiameter / 2,
                 statusBarHealthColor(health));
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(count.c_str(), static_cast<int16_t>(countX + kDotDiameter + kDotGap), midY);
}

#endif  // ARDUINO
