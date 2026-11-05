#include "display/services/ui_flow.h"

#include "airspace/core/cpa_predictor.h"       // extrapolateCpa()
#include "airspace/core/flight_ordering.h"     // splitFeaturedAndRest()
#include "display/core/screens/table_view.h"   // getPageCount(), table row geometry
#include "display/core/theme/lcars_theme.h"    // LCARS_BOTTOM_NAV_HEIGHT
#include "display/core/touch_input.h"          // Rect, hitTest()
#include "platform/core_link.h"                // msUntilPeriod(), heartbeatStale()

// ---- What the screens show -------------------------------------------------------

const std::vector<AircraftRow> &netRows(const NetState &s) {
  static const std::vector<AircraftRow> kNone;
  return s.data != nullptr ? s.data->rows : kNone;
}

CpaPrediction netCpaAt(const NetState &s, uint32_t nowMs) {
  if (s.data == nullptr) return CpaPrediction{};
  return extrapolateCpa(s.data->cpa, nowMs - s.data->polledAtMs);
}

UiView makeUiView(const NetState &s, int screen, int page, OpenSkyHealth shownHealth,
                  uint32_t nowMs, std::time_t local, bool synced) {
  UiView v;
  v.screen = screen;
  v.page = page;
  v.rows = &netRows(s);
  v.hasData = s.data != nullptr;
  v.nowRowIndex = v.hasData ? s.data->nowRowIndex : -1;
  v.cpa = netCpaAt(s, nowMs);
  v.radiusDeg = s.cfg != nullptr ? s.cfg->radius_deg : 0.0f;
  v.health = shownHealth;
  v.alertBadge = s.alerts.displaySeverity();
  v.local = local;
  v.synced = synced;
  v.homeConfigured = s.cfg != nullptr && s.cfg->home_configured;
  return v;
}

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

TapContext tapContextOf(const UiView &v, int16_t screenW, int16_t screenH, Language lang) {
  TapContext c;
  c.currentScreen = v.screen;
  c.page = v.page;
  c.pageCount = tablePageCount(*v.rows);
  c.homeConfigured = v.homeConfigured;
  c.header = statusBarZonesNow(screenW, static_cast<int>(v.rows->size()), v.hasData, lang, v.local,
                               v.synced, v.alertBadge != AlertSeverity::None);
  c.screenW = screenW;
  c.screenH = screenH;
  return c;
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

// ---- Screen changes -------------------------------------------------------------

void ScreenChangeTracker::begin(int screen, uint32_t nowMs) {
  persistedScreen_ = screen;
  persistedAtMs_ = nowMs;
  changedAtMs_ = nowMs;
}

// A touch persists at once, an auto-cycle advance rate-limited
// (docs/review-notes-2026.md 5.9), a screenshot's switch never (spec 10).
ScreenChangeResult ScreenChangeTracker::onChange(int screen, ScreenChangeCause cause,
                                                 uint32_t nowMs, TablePager &pager,
                                                 RedrawEvents &pending) {
  ScreenChangeResult r;
  pager.reset();
  changedAtMs_ = nowMs;  // any change resets the auto-cycle timer
  if (screen != persistedScreen_ &&
      shouldPersistScreenChange(cause, nowMs - persistedAtMs_, kScreenPersistMinIntervalMs)) {
    r.persist = true;
    persistedScreen_ = screen;
    persistedAtMs_ = nowMs;
  }
  pending.screenChanged = true;
  return r;
}

void ScreenChangeTracker::restartAutoCycle(uint32_t nowMs) { changedAtMs_ = nowMs; }

uint32_t ScreenChangeTracker::autoCycleElapsedMs(uint32_t nowMs) const {
  return nowMs - changedAtMs_;
}

// ---- Header clock, NOW countdown --------------------------------------------------

bool MinuteWatch::changed(long minuteNow) {
  if (minuteNow == last_) return false;
  last_ = minuteNow;
  return true;
}

bool CountdownTick::due(int currentScreen, uint32_t nowMs) {
  if (currentScreen != kScreenFlight || nowMs - lastMs_ < 1000) return false;
  lastMs_ = nowMs;
  return true;
}

uint32_t CountdownTick::msUntilNext(uint32_t nowMs) const {
  return msUntilPeriod(nowMs, lastMs_, 1000);
}

// ---- Auto-cycle ---------------------------------------------------------------

bool shouldAutoCycleNow(const AutoCycleInput &in) {
  if (!in.enabled || !in.homeConfigured || in.screenOff) return false;
  const bool deferHold = shouldDeferAutoSwitch(in.currentScreen, in.cpaFound, in.tCpaSeconds);
  return shouldAutoAdvance(in.elapsedMs, in.intervalS, deferHold);
}

AutoCycleInput autoCycleInputAt(const NetState &s, int currentScreen, bool screenOff,
                                uint32_t elapsedMs, uint32_t nowMs) {
  AutoCycleInput in;
  in.enabled = s.cfg->auto_cycle_enabled;
  in.homeConfigured = s.cfg->home_configured;
  in.screenOff = screenOff;
  in.currentScreen = currentScreen;
  const CpaPrediction cpa = netCpaAt(s, nowMs);
  in.cpaFound = cpa.found;
  in.tCpaSeconds = cpa.t_cpa_seconds;
  in.elapsedMs = elapsedMs;
  in.intervalS = s.cfg->auto_cycle_interval_s;
  return in;
}

// ---- The health the header shows ------------------------------------------------

ShownHealthStep shownHealthStep(OpenSkyHealth shownNow, OpenSkyHealth netHealth, uint32_t nowMs,
                                uint32_t netBeatMs, bool rowsEmpty) {
  ShownHealthStep r;
  r.stalled = heartbeatStale(nowMs, netBeatMs, kNetStallLimitMs);
  r.shown = r.stalled ? OpenSkyHealth::NetworkError : netHealth;
  if (r.shown == shownNow) return r;
  r.healthChanged = true;
  r.newPoll = rowsEmpty;  // an empty screen says why it is empty (emptyStateText())
  return r;
}

// ---- Touch edges ----------------------------------------------------------------

bool TouchEdge::tap(bool pressed) {
  const bool tap = pressed && !wasPressed_;
  wasPressed_ = pressed;
  return tap;
}

void TouchEdge::swallow() { wasPressed_ = true; }

// ---- The UI loop's bookkeeping together ---------------------------------------------

void UiFlow::takeNetEvents(const RedrawEvents &e, const NetState &next) {
  pending.firstDraw |= e.firstDraw;
  pending.languageChanged |= e.languageChanged;
  pending.newPoll |= e.newPoll;
  pending.minuteChanged |= e.minuteChanged;
  pending.alertsChanged |= e.alertsChanged;
  if (e.newPoll) pager.clamp(tablePageCount(netRows(next)));  // a shorter list: its last page
}

bool UiFlow::updateHealth(const NetState &s, uint32_t nowMs, uint32_t netBeatMs) {
  const ShownHealthStep h =
      shownHealthStep(shownHealth, s.health, nowMs, netBeatMs, netRows(s).empty());
  if (!h.healthChanged) return false;
  shownHealth = h.shown;
  pending.healthChanged = true;
  pending.newPoll |= h.newPoll;
  return h.stalled;
}

bool UiFlow::screenChanged(int screen, ScreenChangeCause cause, uint32_t nowMs) {
  return screenChanges.onChange(screen, cause, nowMs, pager, pending).persist;
}

bool UiFlow::apply(const ServiceOutcome &o, uint32_t nowMs) {
  if (o.clearPending) pending = RedrawEvents{};
  if (o.firstDraw) pending.firstDraw = true;
  if (o.restartAutoCycle) screenChanges.restartAutoCycle(nowMs);
  if (o.swallowTouch) touch.swallow();
  return o.ownsDisplay;
}

void UiFlow::pageTap(TapKind kind, int pageCount) {
  bool moved = false;
  if (kind == TapKind::PagePrev) moved = pager.prev();
  if (kind == TapKind::PageNext) moved = pager.next(pageCount);
  if (moved) pending.pageChanged = true;
}

bool UiFlow::tick(const NetState &s, int screen, uint32_t nowMs, long minuteNow, bool screenOff) {
  if (countdown.due(screen, nowMs)) pending.countdownTick = true;
  if (minuteWatch.changed(minuteNow)) pending.minuteChanged = true;
  return shouldAutoCycleNow(
      autoCycleInputAt(s, screen, screenOff, screenChanges.autoCycleElapsedMs(nowMs), nowMs));
}

uint8_t UiFlow::takeRedrawMask(int screen) {
  const uint8_t mask = computeRedrawMask(pending, screen);
  pending = RedrawEvents{};
  return mask;
}

void UiFlow::addWakeTimes(UiWake &w, int screen, uint32_t nowMs) const {
  if (screen == kScreenFlight) w.untilCountdownMs = countdown.msUntilNext(nowMs);
}
