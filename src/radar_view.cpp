#include "radar_view.h"

#include <algorithm>

namespace {
// Same km-per-degree approximation table_view's computeDistanceBearing()
// (haversine) is built on, for short distances.
constexpr float kKmPerDegree = 111.2f;
}  // namespace

RadarLayout computeRadarLayout(int16_t x, int16_t y, int16_t w, int16_t h, int16_t margin) {
  RadarLayout layout;

  int16_t usableW = static_cast<int16_t>(w - 2 * margin);
  int16_t usableH = static_cast<int16_t>(h - 2 * margin);
  int16_t diameter = std::min(usableW, usableH);
  if (diameter < 0) diameter = 0;

  layout.radius_px = static_cast<int16_t>(diameter / 2);
  // Round the centre to nearest so an odd content height (y:28..225 -> 197)
  // lands on CLAUDE.md's stated (95, 127) rather than 1px low.
  layout.center_x = static_cast<int16_t>(x + (w + 1) / 2);
  layout.center_y = static_cast<int16_t>(y + (h + 1) / 2);
  return layout;
}

float radiusDegToKm(float radius_deg) { return radius_deg * kKmPerDegree; }

#ifdef ARDUINO

#include <cmath>
#include <cstdio>

#include "aircraft_summary.h"
#include "lcars_theme.h"

namespace {

// Fixed content-area layout on the 320x240 frame — CLAUDE.md "Screen 3".
constexpr int16_t kContentTop = LCARS_CONTENT_TOP;                          // 18
constexpr int16_t kContentHeight = LCARS_CONTENT_BOTTOM - LCARS_CONTENT_TOP;  // 197 (y:18..215)

constexpr int16_t kRadarZoneX = 0;
constexpr int16_t kRadarZoneW = 190;
constexpr int16_t kRadarMargin = 10;  // -> center (95,117), radius 85

// No divider between radar and summary any more (removed on request) —
// the summary text just starts a little right of the radar zone.
constexpr int16_t kSummaryX = 200;
constexpr int16_t kSummaryW = 120;  // x:200..320
constexpr int16_t kSummaryInset = 3;
constexpr int16_t kSummaryTextX = 208;
constexpr int16_t kSummaryFlightY = kContentTop + 52;    // 70  — four rows centred
constexpr int16_t kSummaryAirlineY = kContentTop + 86;   // 104   in the content area
constexpr int16_t kSummaryFromY = kContentTop + 112;     // 130  origin city
constexpr int16_t kSummaryToY = kContentTop + 132;       // 150  "-> destination city"
// Text width between kSummaryTextX and the panel's right edge (less kSummaryInset),
// minus a 2px breathing gap — what fitToWidth() cuts rows 2/3 down to.
constexpr int kSummaryTextMaxPx = kSummaryX + kSummaryW - kSummaryInset - 2 - kSummaryTextX;

constexpr int kRingCount = 3;
constexpr int16_t kBlipRadius = 5;
constexpr int16_t kClampedBlipRadius = 3;
// Muted lavender for out-of-range (clamped) blips — deliberately not the
// sky of in-range blips, so "off the scale" reads distinct.
constexpr uint16_t kClampedBlipColor = THEME_CONTENT_MUTED;
constexpr int16_t kHomeCrosshair = 5;

void drawRadar(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg) {
  RadarLayout layout =
      computeRadarLayout(kRadarZoneX, kContentTop, kRadarZoneW, kContentHeight, kRadarMargin);
  if (layout.radius_px <= 0) return;

  const float maxKm = radiusDegToKm(radius_deg);
  const std::vector<float> ringsKm = computeRingDistances(maxKm, kRingCount);

  // Concentric rings at r = radius_px * 1/3, 2/3, 3/3, each with its
  // km label (lcars_theme::drawRadarRing).
  for (int i = 0; i < kRingCount; ++i) {
    // Round to nearest so radius 85 lands on the spec's 28/57/85, not 28/56/85.
    int16_t ringR = static_cast<int16_t>(
        (layout.radius_px * (i + 1) + kRingCount / 2) / kRingCount);
    if (ringR <= 0) continue;
    int km = static_cast<int>(std::lround(ringsKm[static_cast<size_t>(i)]));
    drawRadarRing(gfx, layout.center_x, layout.center_y, ringR, km, THEME_CONTENT_MUTED);
  }

  // Aircraft blips.
  for (const AircraftRow &row : rows) {
    if (!row.has_distance) continue;
    ScreenPoint p = polarToScreen(row.bearing_deg, row.distance_km, maxKm, layout.center_x,
                                  layout.center_y, layout.radius_px);
    if (p.clamped) {
      gfx.drawCircle(p.x, p.y, kClampedBlipRadius, kClampedBlipColor);
    } else {
      gfx.fillCircle(p.x, p.y, kBlipRadius, THEME_CONTENT_SECONDARY);
    }
  }

  // Home crosshair (drawn last so nothing obscures it).
  gfx.drawFastHLine(static_cast<int16_t>(layout.center_x - kHomeCrosshair), layout.center_y,
                    static_cast<int16_t>(2 * kHomeCrosshair + 1), LCARS_YELLOW);
  gfx.drawFastVLine(layout.center_x, static_cast<int16_t>(layout.center_y - kHomeCrosshair),
                    static_cast<int16_t>(2 * kHomeCrosshair + 1), LCARS_YELLOW);
  gfx.fillCircle(layout.center_x, layout.center_y, 2, LCARS_YELLOW);
}

void drawSummaryPanel(LGFX &gfx, const std::vector<AircraftRow> &rows) {
  // No frame (dropped by request) — the orange divider alone separates the
  // panel from the radar. Keep any long text from spilling past the panel.
  gfx.setClipRect(static_cast<int16_t>(kSummaryX + kSummaryInset), kContentTop,
                  static_cast<int16_t>(kSummaryW - 2 * kSummaryInset), kContentHeight);

  if (rows.empty()) {
    gfx.setFont(LCARS_FONT_BODY);
    gfx.setTextDatum(middle_center);
    gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
    gfx.drawString(tr(Str::NoFlight), static_cast<int16_t>(kSummaryX + kSummaryW / 2),
                   static_cast<int16_t>(kContentTop + kContentHeight / 2));
    gfx.clearClipRect();
    return;
  }

  const AircraftRow &nearest = rows.front();

  gfx.setTextDatum(top_left);

  // Row 1: callsign, emphasized.
  gfx.setFont(LCARS_FONT_HEADING);
  gfx.setTextColor(THEME_CONTENT_PRIMARY, LCARS_BLACK);
  gfx.drawString(summaryFlight(nearest).c_str(), kSummaryTextX, kSummaryFlightY);

  // Row 2: airline, cut to the panel's text width (setClipRect above is
  // only the pixel-level backstop).
  gfx.setFont(LCARS_FONT_BODY);
  gfx.setTextColor(THEME_CONTENT_SECONDARY, LCARS_BLACK);
  const TextMeasure measure = gfxTextMeasure(gfx);
  gfx.drawString(fitToWidth(summaryAirline(nearest), kSummaryTextMaxPx, measure).c_str(),
                 kSummaryTextX, kSummaryAirlineY);

  // Rows 3-4: route as city names, one per line (the panel is too narrow
  // for "Warszawa -> Rzym" on one line): "Warszawa" / "-> Rzym".
  gfx.drawString(fitToWidth(rowPlaceName(nearest, /*origin=*/true, i18nLanguage()),
                            kSummaryTextMaxPx, measure)
                     .c_str(),
                 kSummaryTextX, kSummaryFromY);
  if (nearest.route.found) {
    gfx.drawString(fitToWidth("-> " + rowPlaceName(nearest, /*origin=*/false, i18nLanguage()),
                              kSummaryTextMaxPx, measure)
                       .c_str(),
                   kSummaryTextX, kSummaryToY);
  }

  gfx.clearClipRect();
}

}  // namespace

void drawRadarView(LGFX &gfx, const std::vector<AircraftRow> &rows, float radius_deg,
                   int16_t screenWidth) {
  (void)screenWidth;  // fixed 320px layout — parameter kept for signature symmetry

  drawRadar(gfx, rows, radius_deg);
  drawSummaryPanel(gfx, rows);
}

#endif  // ARDUINO
