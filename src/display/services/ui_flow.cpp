#include "display/services/ui_flow.h"

// TODO(spec 12): stub from test-author
TapAction routeTap(int16_t /*x*/, int16_t /*y*/, const TapContext & /*c*/) { return {}; }

// TODO(spec 12): stub from test-author
int tablePageCount(const std::vector<AircraftRow> & /*rows*/) { return 0; }

// TODO(spec 12): stub from test-author
int TablePager::page() const { return 0; }
// TODO(spec 12): stub from test-author
void TablePager::reset() {}
// TODO(spec 12): stub from test-author
void TablePager::clamp(int /*pageCount*/) {}
// TODO(spec 12): stub from test-author
bool TablePager::prev() { return false; }
// TODO(spec 12): stub from test-author
bool TablePager::next(int /*pageCount*/) { return false; }

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

// TODO(spec 12): stub from test-author
bool TouchEdge::tap(bool /*pressed*/) { return false; }
// TODO(spec 12): stub from test-author
void TouchEdge::swallow() {}
