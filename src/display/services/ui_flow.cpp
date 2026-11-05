#include "display/services/ui_flow.h"

#include "airspace/core/flight_ordering.h"     // splitFeaturedAndRest()
#include "display/core/screens/table_view.h"   // getPageCount(), table row geometry
#include "display/core/theme/lcars_theme.h"    // LCARS_BOTTOM_NAV_HEIGHT
#include "display/core/touch_input.h"          // Rect, hitTest()
#include "platform/core_link.h"                // msUntilPeriod(), heartbeatStale()

// ---- Taps ---------------------------------------------------------------------

TapAction routeTap(int16_t x, int16_t y, const TapContext &c) {
  TapAction a;
  // The "!" first: its zone reaches a little into the content area.
  if (c.homeConfigured && alertBadgeHit(x, y, c.header)) {
    a.kind = TapKind::OpenAlerts;
    return a;
  }

  // Screens switch only via the bottom nav buttons.
  const NavHit hit = navHitTest(x, y, c.screenW, c.screenH);
  if (hit.action == NavAction::JumpTo) {
    a.kind = TapKind::JumpTo;
    a.target = hit.target;
    return a;
  }

  // Anywhere else: only the Flights table reacts (page the rows). The strips
  // are table_view's own row height, so the two can't drift apart
  // (docs/review-notes-2026.md 5.5).
  if (c.currentScreen != kScreenFlights) return a;
  const int16_t stripH = tableRowHeightPx();
  Rect prevZone;
  prevZone.x = 0;
  prevZone.y = tableFirstRowY();  // top of the data-row area
  prevZone.w = c.screenW;
  prevZone.h = stripH;
  Rect nextZone = prevZone;
  nextZone.y = static_cast<int16_t>(c.screenH - LCARS_BOTTOM_NAV_HEIGHT - stripH);

  if (hitTest(x, y, prevZone) && c.page > 0) {
    a.kind = TapKind::PagePrev;
  } else if (hitTest(x, y, nextZone) && c.page < c.pageCount - 1) {
    a.kind = TapKind::PageNext;
  }
  return a;
}

int tablePageCount(const std::vector<AircraftRow> &rows) {
  const FeaturedSplit split = splitFeaturedAndRest(rows);
  return getPageCount(static_cast<int>(split.rest.size()), tableRowsPerPage());
}

int TablePager::page() const { return page_; }

void TablePager::reset() { page_ = 0; }

void TablePager::clamp(int pageCount) {
  if (page_ >= pageCount) page_ = pageCount > 0 ? pageCount - 1 : 0;
}

bool TablePager::prev() {
  if (page_ <= 0) return false;
  --page_;
  return true;
}

bool TablePager::next(int pageCount) {
  if (page_ >= pageCount - 1) return false;
  ++page_;
  return true;
}

// TODO(spec 12): stub from test-author
void ScreenChangeTracker::begin(int /*screen*/, uint32_t /*nowMs*/) {}
// TODO(spec 12): stub from test-author
ScreenChangeResult ScreenChangeTracker::onChange(int /*screen*/, ScreenChangeCause /*cause*/,
                                                 uint32_t /*nowMs*/, TablePager & /*pager*/,
                                                 RedrawEvents & /*pending*/) {
  return {};
}
// TODO(spec 12): stub from test-author
void ScreenChangeTracker::restartAutoCycle(uint32_t /*nowMs*/) {}
// TODO(spec 12): stub from test-author
uint32_t ScreenChangeTracker::autoCycleElapsedMs(uint32_t /*nowMs*/) const { return 0; }

// TODO(spec 12): stub from test-author
bool MinuteWatch::changed(long /*minuteNow*/) { return false; }

// TODO(spec 12): stub from test-author
bool CountdownTick::due(int /*currentScreen*/, uint32_t /*nowMs*/) { return false; }
// TODO(spec 12): stub from test-author
uint32_t CountdownTick::msUntilNext(uint32_t /*nowMs*/) const { return 0; }

// TODO(spec 12): stub from test-author
bool shouldAutoCycleNow(const AutoCycleInput & /*in*/) { return false; }

// TODO(spec 12): stub from test-author
ShownHealthStep shownHealthStep(OpenSkyHealth /*shownNow*/, OpenSkyHealth /*netHealth*/,
                                uint32_t /*nowMs*/, uint32_t /*netBeatMs*/, bool /*rowsEmpty*/) {
  return {};
}

// ---- Touch edges ----------------------------------------------------------------

bool TouchEdge::tap(bool pressed) {
  const bool tap = pressed && !wasPressed_;
  wasPressed_ = pressed;
  return tap;
}

void TouchEdge::swallow() { wasPressed_ = true; }
