#include "display/adapters/lgfx/status_bar_draw.h"

#include "display/adapters/lgfx/lcars_theme_draw.h"  // palette, fonts

void drawStatusBar(LGFX &gfx, int aircraftCount, bool hasData, FeedHealth health,
                   AlertSeverity badge, std::time_t localEpoch, bool timeSynced,
                   int16_t screenWidth) {
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
  int leftEnd = z.name_x + static_cast<int>(gfx.textWidth(kStatusBarAppName));

  // The device-alert "!" right after the name (spec 08 §3.4); the touch zone
  // in screen_nav (alertBadgeHit()) follows statusBarZones()' alert_x.
  if (badge != AlertSeverity::None) {
    const int x = leftEnd + kAlertBadgeGap;
    const int r = kAlertBadgeDiameter / 2;
    const int16_t cx = static_cast<int16_t>(x + r);
    const int16_t cy = static_cast<int16_t>(kAlertBadgeTop + r);
    gfx.fillCircle(cx, cy, r, alertBadgeColor(badge));
    // The "!" by hand (2 px wide, crisp at any font): a bar and a dot.
    gfx.fillRect(static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy - 4), 2, 6, THEME_ON_FILL);
    gfx.fillRect(static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy + 3), 2, 2, THEME_ON_FILL);
    leftEnd = x + kAlertBadgeDiameter;
  }
  const int countW = kStatusBarDotDiameter + kStatusBarDotGap + static_cast<int>(gfx.textWidth(count.c_str()));

  // Right: date + clock, both in the clock font (crisp dots — review #9).
  // The date stays light peach; the time is white so it still reads first.
  gfx.setFont(LCARS_FONT_CLOCK);
  const int clockW = static_cast<int>(gfx.textWidth(clock.c_str()));
  const int dateW = static_cast<int>(gfx.textWidth(date.c_str()));
  const int dateX = z.right_end - clockW - kStatusBarDateClockGap - dateW;
  gfx.setTextColor(THEME_CHROME_TEXT, LCARS_BLACK);
  gfx.drawString(date.c_str(), static_cast<int16_t>(dateX), midY);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.setTextDatum(middle_right);
  gfx.drawString(clock.c_str(), static_cast<int16_t>(z.right_end), midY);

  // Centre: health dot + flight count, centred in the gap between the two.
  const int countX = (leftEnd + dateX) / 2 - countW / 2;
  gfx.fillCircle(static_cast<int16_t>(countX + kStatusBarDotDiameter / 2), midY, kStatusBarDotDiameter / 2,
                 statusBarHealthColor(health));
  gfx.setFont(LCARS_FONT_SMALL);
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(count.c_str(), static_cast<int16_t>(countX + kStatusBarDotDiameter + kStatusBarDotGap), midY);
}
