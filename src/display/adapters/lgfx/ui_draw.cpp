#include "display/adapters/lgfx/ui_draw.h"

#include "display/adapters/lgfx/flight_screen_draw.h"
#include "display/adapters/lgfx/lcars_theme_draw.h"
#include "display/adapters/lgfx/radar_view_draw.h"
#include "display/adapters/lgfx/status_bar_draw.h"
#include "display/adapters/lgfx/table_view_draw.h"
#include "display/core/i18n.h"
#include "display/core/redraw_policy.h"
#include "display/core/theme/lcars_theme.h"

namespace {

// The active screen's content, drawn over whatever is there — callers clear first.
void drawActiveContent(LGFX &tft, const UiView &v) {
  const int16_t screenW = static_cast<int16_t>(tft.width());
  const std::vector<AircraftRow> &rows = *v.rows;
  switch (v.screen) {
    case kScreenFlight: {
      static const AircraftRow kNoRow{};
      const bool hasNow = v.nowRowIndex >= 0 && v.nowRowIndex < static_cast<int>(rows.size());
      drawFlightScreen(tft, hasNow ? rows[static_cast<size_t>(v.nowRowIndex)] : kNoRow, hasNow,
                       v.cpa, screenW, v.health);
      break;
    }
    case kScreenRadar:
      drawRadarView(tft, rows, v.radiusDeg, screenW, v.health);
      break;
    case kScreenFlights:
    default:
      drawTablePage(tft, rows, screenW, v.page, v.health);
      break;
  }
}

// Header bar: aircraft count + health dot + device-alert "!" + date/time.
void drawHeader(LGFX &tft, const UiView &v) {
  drawStatusBar(tft, static_cast<int>(v.rows->size()), v.hasData, v.health, v.alertBadge, v.local,
                v.synced, static_cast<int16_t>(tft.width()));
}

}  // namespace

void applyRedraw(LGFX &tft, uint8_t mask, const UiView &v) {
  const int16_t screenW = static_cast<int16_t>(tft.width());
  const int16_t screenH = static_cast<int16_t>(tft.height());

  if (mask & kRedrawFull) {
    // Structural change: clear everything, then header, content, nav bar.
    tft.fillScreen(LCARS_BLACK);
    drawHeader(tft, v);
    drawActiveContent(tft, v);
    drawBottomNav(tft, v.screen, screenW, screenH, statusBarScreenName);
    return;
  }

  if (mask & kRedrawContent) {
    // Only the band between header and bottom nav; chrome is left alone.
    const int16_t contentBottom = static_cast<int16_t>(screenH - LCARS_BOTTOM_NAV_HEIGHT);
    tft.fillRect(0, LCARS_HEADER_HEIGHT, screenW,
                 static_cast<int16_t>(contentBottom - LCARS_HEADER_HEIGHT), LCARS_BLACK);
    drawActiveContent(tft, v);
  }

  if (mask & kRedrawHeader) {
    drawHeader(tft, v);
  }

  if (mask & kRedrawCountdown) {
    drawFlightCountdown(tft, !v.rows->empty(), v.cpa, screenW, v.health);
  }
}

void drawSetupPrompt(LGFX &tft) {
  const int16_t screenW = static_cast<int16_t>(tft.width());
  const int16_t screenH = static_cast<int16_t>(tft.height());

  tft.fillScreen(LCARS_BLACK);
  tft.setFont(LCARS_FONT_BODY);
  tft.setTextDatum(middle_center);
  tft.setTextColor(THEME_CHROME_ACCENT, LCARS_BLACK);
  tft.drawString(tr(Str::SetupPrompt), screenW / 2, screenH / 2 - 10);
  tft.drawString("http://cyd-sky.local", screenW / 2, screenH / 2 + 10);
}
