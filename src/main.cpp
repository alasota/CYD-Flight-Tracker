// CYD Sky Tracker — main orchestration.
//
// setup(): initializes LovyanGFX (configured via include/LGFX_CYD.hpp) and
// the XPT2046 touch controller (touch_input), loads the persisted config
// (config_store) — restoring which of the three screens was last active
// (last_screen 0/1/2) into screen_nav — connects WiFi (wifi_manager —
// falls back to its own captive portal if no saved network works), starts
// NTP (time_sync) and the local config web page (config_portal) at
// cyd-sky.local in the background.
//
// loop(): non-blocking, millis()-based.
//   - Touch (edge-detected, one action per press): screen_nav::navHitTest
//     classifies the tap — a left/right edge strip is prev/next screen, a
//     bottom-nav segment jumps straight to that screen; every screen
//     change persists to config_store (last_screen) and forces a redraw.
//     Otherwise, on the Flights screen, a tap in the top/bottom row strip
//     pages the table (swap which 5 rows show — never scroll).
//   - Once per config_store's poll_interval_s (opensky_client's own
//     scheduler): fetch Aircraft[] from OpenSky; if home isn't configured
//     yet (config_store's home_configured) show a setup prompt instead;
//     otherwise enrich each row (aircraft_lookup + route_lookup, both
//     cached, together capped at kMaxNewLookupsPerPoll *new* HTTP calls
//     per cycle — review notes 1.1), annotate distance + flight phase,
//     sort by distance, look up the nearest aircraft's route airports,
//     and recompute its closest-point-of-approach time (cpa_predictor).
//   - The Flight screen's countdown ticks locally every second between
//     polls: currentCpa() extrapolates lastPolledCpa forward by the
//     elapsed millis(), and the screen is flagged for redraw at 1 Hz
//     while it's active (see CLAUDE.md "local ticking").
//   - Auto screen cycling (off unless config_store's auto_cycle_enabled):
//     a millis() timer from the last screen change (manual or automatic);
//     once it passes auto_cycle_interval_s, advance via nav.next() —
//     unless screen_nav::shouldDeferAutoSwitch() says an overhead moment
//     is imminent on the Flight screen, in which case hold and re-check
//     every loop until it clears. screen_nav::shouldAutoAdvance() is the
//     pure decision; every screen change resets the timer.
//   - Redraw (only when something changed): status_bar header, then the
//     active screen's content (table_view / flight_screen / radar_view)
//     dispatched on screen_nav::current(), then the persistent bottom nav
//     bar.
//
// This file is deliberately thin: all actual logic lives in the modules
// above — see CLAUDE.md "Testing" for why only this kind of orchestration
// code is allowed to go untested by Unity.

#include <Arduino.h>

#include <map>
#include <string>
#include <vector>

#include "LGFX_CYD.hpp"
#include "aircraft_lookup.h"
#include "config_portal.h"
#include "config_store.h"
#include "cpa_predictor.h"
#include "flight_screen.h"
#include "lcars_theme.h"
#include "opensky_client.h"
#include "radar_view.h"
#include "route_lookup.h"
#include "screen_nav.h"
#include "status_bar.h"
#include "table_view.h"
#include "time_sync.h"
#include "touch_input.h"
#include "wifi_manager.h"

static LGFX tft;

// The active screen (0 Flights / 1 Flight / 2 Radar). Seeded from
// config_store's last_screen at boot; current() is re-persisted whenever a
// touch changes it.
static ScreenNav nav;

// Data + paging state for the currently-drawn screen. Persists across
// loop() iterations so a touch-driven page/screen change can redraw
// immediately without waiting for the next OpenSky poll.
static std::vector<AircraftRow> lastRows;
static AirportInfo lastOriginAirport;  // nearest row's route origin, if resolved
static AirportInfo lastDestAirport;    // nearest row's route destination, if resolved
static float lastRadiusDeg = 2.5f;     // config_store's default — updated every poll

// Auto screen cycling state (CLAUDE.md "Auto screen cycling"). The two
// config values are cached (refreshed at boot and every poll) so loop()
// doesn't hit NVS every iteration; lastScreenChangeMs is the timer base,
// reset by onScreenChanged() on every screen change — manual or auto.
static bool autoCycleEnabled = false;
static uint32_t autoCycleIntervalS = 15;
static bool homeConfigured = false;
static uint32_t lastScreenChangeMs = 0;
static int currentPage = 0;
static bool screenNeedsRedraw = false;
static bool setupPromptShown = false;

// Closest-point-of-approach for the nearest aircraft, as computed at the
// last poll, plus the millis() timestamp of that poll — currentCpa()
// extrapolates between polls so the Flight screen's countdown ticks every
// second (CLAUDE.md "local ticking between polls").
static CpaPrediction lastPolledCpa;
static uint32_t lastCpaPollMs = 0;

// Height of the table's page-back / page-forward tap strips — table_view's
// own row height, so the two can't drift apart (review notes 5.5).
static const int16_t kPageTapZoneHeight = tableRowHeightPx();

// How many *new* (uncached) aircraft_lookup/route_lookup HTTP calls one
// poll cycle is allowed to make, combined — each blocks loop() for up to a
// few seconds (review notes 1.1). Aircraft past the budget render as "--"
// this cycle and get their turn on a later poll.
static constexpr int kMaxNewLookupsPerPoll = 5;

// flight_phase thresholds for classifyPhases() (CLAUDE.md "Flight phase").
// near_airport_km is really "near HOME" — this project has no airport
// coordinate database, only country codes, so distance from the configured
// home position is what's actually available.
static constexpr float kNearAirportKm = 15.0f;
static constexpr float kClimbThresholdMps = 3.0f;

// Nearest aircraft's CPA, extrapolated from the last poll to "now".
static CpaPrediction currentCpa() {
  CpaPrediction p = lastPolledCpa;
  if (p.found) {
    p.t_cpa_seconds -= static_cast<float>(millis() - lastCpaPollMs) / 1000.0f;
  }
  return p;
}

static void redrawScreen() {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  tft.fillScreen(LCARS_BLACK);

  // Shared header chrome, then the active screen's content, then the nav bar.
  drawStatusBar(tft, nav.current(), timeSyncNowLocal(), isTimeSynced(), screenW);

  switch (nav.current()) {
    case kScreenFlight: {
      static const AircraftRow kNoRow{};
      bool hasNearest = !lastRows.empty();
      drawFlightScreen(tft, hasNearest ? lastRows.front() : kNoRow, hasNearest, lastOriginAirport,
                       lastDestAirport, currentCpa(), screenW);
      break;
    }
    case kScreenRadar:
      drawRadarView(tft, lastRows, lastRadiusDeg, lastOriginAirport, lastDestAirport, screenW);
      break;
    case kScreenFlights:
    default:
      drawTablePage(tft, lastRows, lastOriginAirport, lastDestAirport, screenW, currentPage);
      break;
  }

  drawBottomNav(tft, nav.current(), screenW, screenH);
}

// Shown instead of a screen while config_store's home_configured is still
// false (review notes 1.5). Drawn once (guarded) rather than every poll.
static void showSetupPrompt() {
  if (setupPromptShown) return;

  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  tft.fillScreen(LCARS_BLACK);
  tft.setFont(LCARS_FONT_BODY);
  tft.setTextDatum(middle_center);
  tft.setTextColor(LCARS_AMBER, LCARS_BLACK);
  tft.drawString("Set your home location at", screenW / 2, screenH / 2 - 10);
  tft.drawString("http://cyd-sky.local", screenW / 2, screenH / 2 + 10);

  setupPromptShown = true;
}

// Persist + redraw after screen_nav reports the active screen changed.
// Called for every change — a touch, or an auto-cycle advance.
static void onScreenChanged() {
  currentPage = 0;
  lastScreenChangeMs = millis();  // any change resets the auto-cycle timer

  saveLastScreen(nav.current());  // targeted single-key write (see config_store)

  screenNeedsRedraw = true;
}

// Dispatches one touch tap (edge-detected in loop(), once per press).
static void handleTap(int16_t x, int16_t y) {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  NavHit hit = navHitTest(x, y, screenW, screenH, LCARS_HEADER_HEIGHT);
  switch (hit.action) {
    case NavAction::Prev:
      if (nav.prev()) onScreenChanged();
      return;
    case NavAction::Next:
      if (nav.next()) onScreenChanged();
      return;
    case NavAction::JumpTo:
      if (nav.set(hit.target)) onScreenChanged();
      return;
    case NavAction::None:
      break;
  }

  // Middle of the screen: only the Flights table reacts (page the rows).
  if (nav.current() != kScreenFlights) return;

  FeaturedSplit split = splitFeaturedAndRest(lastRows);
  int pageCount = getPageCount(static_cast<int>(split.rest.size()), tableRowsPerPage());

  Rect prevZone;
  prevZone.x = 0;
  prevZone.y = tableFirstRowY();  // top of the data-row area
  prevZone.w = screenW;
  prevZone.h = kPageTapZoneHeight;

  Rect nextZone;
  nextZone.x = 0;
  nextZone.y = static_cast<int16_t>(screenH - LCARS_BOTTOM_NAV_HEIGHT - kPageTapZoneHeight);
  nextZone.w = screenW;
  nextZone.h = kPageTapZoneHeight;

  if (hitTest(x, y, prevZone) && currentPage > 0) {
    currentPage--;
    screenNeedsRedraw = true;
  } else if (hitTest(x, y, nextZone) && currentPage < pageCount - 1) {
    currentPage++;
    screenNeedsRedraw = true;
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(LCARS_BLACK);

  touchInputBegin();

  Config cfg = loadConfig();
  nav.set(cfg.last_screen);  // restore the last-active screen
  lastRadiusDeg = cfg.radius_deg;
  autoCycleEnabled = cfg.auto_cycle_enabled;
  autoCycleIntervalS = cfg.auto_cycle_interval_s;
  homeConfigured = cfg.home_configured;
  lastScreenChangeMs = millis();

  wifiManagerBegin();  // tries the saved network; falls back to its own captive portal

  timeSyncBegin();  // NTP (Europe/Warsaw), after WiFi; header shows --:-- until it lands

  // Starts mDNS + the config WebServer right away, non-blocking. In the
  // common case (a saved network that's reachable) wifiManagerBegin()
  // above has already connected by the time we get here; in the portal
  // fallback case cyd-sky.local won't resolve until the device later joins
  // a real network — the captive portal covers initial setup then.
  configPortalBegin();

  screenNeedsRedraw = true;  // paint the (empty) active screen + chrome immediately
}

void loop() {
  wifiManagerLoop();
  configPortalLoop();

  // --- Touch: edge-detected, one handleTap() per press --------------------
  static bool wasTouchPressed = false;
  TouchPoint touch = touchInputRead();
  if (touch.pressed && !wasTouchPressed) {
    handleTap(touch.x, touch.y);
  }
  wasTouchPressed = touch.pressed;

  // --- OpenSky polling (only meaningful once connected) -------------------
  if (wifiManagerStatus() == WifiStatus::Connected) {
    std::vector<Aircraft> aircraft;
    if (openSkyClientPoll(millis(), &aircraft)) {
      Config homeCfg = loadConfig();
      autoCycleEnabled = homeCfg.auto_cycle_enabled;
      autoCycleIntervalS = homeCfg.auto_cycle_interval_s;
      homeConfigured = homeCfg.home_configured;

      if (!homeCfg.home_configured) {
        showSetupPrompt();
      } else {
        lastRadiusDeg = homeCfg.radius_deg;

        std::map<std::string, AircraftInfo> infoByIcao24;
        std::map<std::string, RouteInfo> routeByCallsign;
        int newLookupsThisCycle = 0;

        for (const Aircraft &ac : aircraft) {
          bool infoCached = aircraftLookupIsCached(ac.icao24);
          if (infoCached || newLookupsThisCycle < kMaxNewLookupsPerPoll) {
            if (!infoCached) newLookupsThisCycle++;
            infoByIcao24[ac.icao24] = lookupAircraft(ac.icao24);
          }
          // else: leave this icao24 out for now — rendered as "--", looked
          // up once back under budget on a later poll.

          bool routeCached = routeLookupIsCached(ac.callsign);
          if (routeCached || newLookupsThisCycle < kMaxNewLookupsPerPoll) {
            if (!routeCached) newLookupsThisCycle++;
            routeByCallsign[ac.callsign] = lookupRoute(ac.callsign);
          }
        }

        lastRows = buildEnrichedRecords(aircraft, infoByIcao24, routeByCallsign);
        annotateDistances(lastRows, homeCfg.home_lat, homeCfg.home_lon);
        classifyPhases(lastRows, kNearAirportKm, kClimbThresholdMps);
        sortRowsByDistance(lastRows);

        // The nearest aircraft's route-endpoint airports, for the identity
        // line's "WAW (PL) -> FCO (IT)" — 2 bounded calls, for that one row.
        lastOriginAirport = AirportInfo{};
        lastDestAirport = AirportInfo{};
        if (!lastRows.empty() && lastRows.front().route.found) {
          lastOriginAirport = lookupAirportCountry(lastRows.front().route.origin_icao);
          lastDestAirport = lookupAirportCountry(lastRows.front().route.dest_icao);
        }

        // Recompute the nearest aircraft's CPA time; currentCpa() ticks it
        // forward between polls.
        lastPolledCpa = CpaPrediction{};
        if (!lastRows.empty() && lastRows.front().aircraft.has_position) {
          const Aircraft &ac = lastRows.front().aircraft;
          lastPolledCpa = predictCpa(homeCfg.home_lat, homeCfg.home_lon, ac.lat, ac.lon, ac.velocity,
                                     ac.true_track);
        }
        lastCpaPollMs = millis();

        FeaturedSplit split = splitFeaturedAndRest(lastRows);
        int pageCount = getPageCount(static_cast<int>(split.rest.size()), tableRowsPerPage());
        if (currentPage >= pageCount) {
          currentPage = pageCount > 0 ? pageCount - 1 : 0;
        }

        screenNeedsRedraw = true;
      }
    }
  }

  // --- Flight screen: tick the countdown ~1 Hz between polls --------------
  static uint32_t lastFlightTickMs = 0;
  if (nav.current() == kScreenFlight && (millis() - lastFlightTickMs) >= 1000) {
    lastFlightTickMs = millis();
    screenNeedsRedraw = true;
  }

  // --- Redraw the header clock once the first NTP sync lands -------------
  static bool clockAppeared = false;
  if (!clockAppeared && isTimeSynced()) {
    clockAppeared = true;
    screenNeedsRedraw = true;
  }

  // --- Auto screen cycling (CLAUDE.md "Auto screen cycling") ------------
  if (autoCycleEnabled && homeConfigured) {
    CpaPrediction cpa = currentCpa();
    bool deferHold = shouldDeferAutoSwitch(nav.current(), cpa.found, cpa.t_cpa_seconds);
    if (shouldAutoAdvance(millis() - lastScreenChangeMs, autoCycleIntervalS, deferHold)) {
      nav.next();          // 3 distinct screens, wraps — always a real change
      onScreenChanged();   // persists last_screen, resets the timer + page, forces redraw
    }
  }

  // --- Redraw, if anything changed --------------------------------------
  if (screenNeedsRedraw) {
    redrawScreen();
    screenNeedsRedraw = false;
  }
}
