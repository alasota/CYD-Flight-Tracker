#include "featured_panel.h"

#include <cstdio>

#include "table_view.h"  // full AircraftRow definition

namespace {
constexpr int16_t kPanelTop = LCARS_CONTENT_TOP;  // 18
constexpr int16_t kPanelHeight = 77;  // y:18..95 per docs/screens.md "Screen 1"
}  // namespace

int16_t featuredPanelTopPx() { return kPanelTop; }
int16_t featuredPanelHeightPx() { return kPanelHeight; }

namespace {
// OpenSky reports ground speed in m/s; km/h reads far better on a display.
constexpr float kMpsToKmh = 3.6f;

// "<n><unit>" rounded to a whole number, or "--" when the value is unknown.
std::string pillValue(bool known, float value, const char *unit) {
  if (!known) return kUnknownText;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%.0f%s", static_cast<double>(value), unit);
  return buf;
}
}  // namespace

std::vector<std::string> featuredPillTexts(const AircraftRow &row, Language lang) {
  // Distance with its compass direction ("S 4km") — the arrow drawn next to
  // it shows the same thing graphically.
  const std::string distance =
      row.has_distance
          ? std::string(compassPoint(row.bearing_deg)) + " " + pillValue(true, row.distance_km, "km")
          : kUnknownText;
  if (row.aircraft.on_ground) {
    return {tr(Str::OnGround, lang), distance};
  }
  // "--" when OpenSky sent null — never a misleading "0m" / "0km/h".
  return {pillValue(row.aircraft.has_altitude, row.aircraft.baro_altitude, "m"),
          pillValue(row.aircraft.has_velocity, row.aircraft.velocity * kMpsToKmh, "km/h"),
          distance};
}

#ifdef ARDUINO

namespace {
constexpr int16_t kSideMargin = 4;     // panel inset from the screen edges
constexpr int16_t kContentInset = 3;   // extra text inset inside the panel area
constexpr int16_t kIdentityY = kPanelTop + 6;   // identity line
constexpr int16_t kRouteY = kPanelTop + 28;     // "WAW (PL) -> FCO (IT)" line
constexpr int16_t kPillY = kPanelTop + 50;      // altitude / speed / distance chips
constexpr int16_t kPillHeight = 22;
constexpr int16_t kPillGap = 4;
constexpr int16_t kArrowSize = 12;  // "where to look" arrow in the distance pill
}  // namespace

void drawFeaturedPanel(LGFX &gfx, const AircraftRow &row, int16_t screenWidth) {
  int16_t w = static_cast<int16_t>(screenWidth - 2 * kSideMargin);

  // No frame (dropped by request) — just text and pills on black.
  // Identity line — phase icon pinned at the panel's right edge,
  // airline/type cut so nothing overflows.
  const int16_t textX = static_cast<int16_t>(kSideMargin + kContentInset + 4);
  const int16_t textRightX = static_cast<int16_t>(kSideMargin + w - kContentInset - 4);
  drawIdentityLine(gfx, row, textX, kIdentityY, textRightX);

  // Route line with city names + country codes ("Warszawa (PL) -> Rzym (IT)").
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.setTextDatum(top_left);
  const int16_t routeX = textX;
  gfx.drawString(
      formatRowRouteCities(row, i18nLanguage(), textRightX - routeX, gfxTextMeasure(gfx))
          .c_str(),
      routeX, kRouteY);

  // Altitude / speed / distance pills along the bottom of the frame.
  int16_t pillAreaX = static_cast<int16_t>(kSideMargin + kContentInset + 2);
  int16_t pillAreaW = static_cast<int16_t>(w - 2 * (kContentInset + 2));
  int16_t pillW = static_cast<int16_t>((pillAreaW - 2 * kPillGap) / 3);

  // 3 pills in the air; on the ground the first ("NA ZIEMI") takes the
  // width of two, so the row still lines up with the panel edges.
  const std::vector<std::string> pills = featuredPillTexts(row, i18nLanguage());
  int16_t x = pillAreaX;
  for (size_t i = 0; i < pills.size(); ++i) {
    const bool wide = (pills.size() == 2 && i == 0);
    const int16_t pw = wide ? static_cast<int16_t>(2 * pillW + kPillGap) : pillW;
    drawPillButton(gfx, x, kPillY, pw, kPillHeight, THEME_PILL_FILL, THEME_CONTENT_SECONDARY,
                   pills[i].c_str());
    // Last pill = distance: add the "where to look" arrow (bearing from
    // home, screen top = north) at its left end.
    if (i + 1 == pills.size() && row.has_distance) {
      drawDirectionArrow(gfx, row.bearing_deg, static_cast<int16_t>(x + kPillHeight / 2 + 2),
                         static_cast<int16_t>(kPillY + kPillHeight / 2), kArrowSize,
                         THEME_CONTENT_PRIMARY);
    }
    x = static_cast<int16_t>(x + pw + kPillGap);
  }
}

void drawFeaturedPanelEmpty(LGFX &gfx, int16_t screenWidth, OpenSkyHealth health) {
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  gfx.drawString(tr(emptyStateText(health, false)), static_cast<int16_t>(screenWidth / 2),
                 static_cast<int16_t>(kPanelTop + kPanelHeight / 2));
}

#endif  // ARDUINO
