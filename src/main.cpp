// CYD Sky Tracker — main orchestration.
//
// setup(): initializes LovyanGFX (configured via include/LGFX_CYD.hpp) and
// its built-in XPT2046 touch driver (read via touch_input), loads the persisted config
// (config_store) — restoring which of the three screens was last active
// (last_screen 0/1/2) into screen_nav — connects WiFi (wifi_manager —
// falls back to its own captive portal if no saved network works), starts
// NTP (time_sync) and the local config web page (config_portal) at
// cyd-sky.local in the background.
//
// loop(): non-blocking, millis()-based.
//   - Touch (edge-detected, one action per press): screen_nav::navHitTest
//     classifies the tap — only a bottom-nav button switches screens (it
//     jumps straight to that screen; no edge-strip swiping); every screen
//     change persists to config_store (last_screen) and forces a redraw.
//     Otherwise, on the Flights screen, a tap in the top/bottom row strip
//     pages the table (swap which 5 rows show — never scroll).
//   - Once per config_store's poll_interval_s (opensky_client's own
//     scheduler): fetch Aircraft[] from OpenSky. A failed poll (auth /
//     rate-limit / network) keeps the last good list on screen and shows a
//     header block in a warning colour rather than blanking (review notes 1.2-1.4); if
//     home isn't configured yet a setup prompt is shown instead. On a
//     clean poll: rank aircraft nearest-first, then enrich only as many as
//     lookup_budget's planLookups() allows new HTTP calls for this cycle
//     (kDefaultLookupBudget — review notes 1.1/3.2), annotate distance +
//     flight phase, sort, look up the nearest aircraft's route airports,
//     and recompute its closest-point-of-approach time (cpa_predictor).
//   - The Flight screen's countdown ticks locally every second between
//     polls: currentCpa() extrapolates lastPolledCpa forward by the
//     elapsed millis(), and a 1 Hz countdown tick is raised while it's
//     active (see CLAUDE.md "local ticking") — repainting only the
//     countdown zone, never the whole screen.
//   - Auto screen cycling (off unless config_store's auto_cycle_enabled):
//     a millis() timer from the last screen change (manual or automatic);
//     once it passes auto_cycle_interval_s, advance via nav.next() —
//     unless screen_nav::shouldDeferAutoSwitch() says an overhead moment
//     is imminent on the Flight screen, in which case hold and re-check
//     every loop until it clears. screen_nav::shouldAutoAdvance() is the
//     pure decision; every screen change resets the timer.
//   - Redraw (only what changed — CLAUDE.md "Redraw policy"): loop()
//     collects RedrawEvents and redraw_policy::computeRedrawMask() picks
//     the regions. A full clear (fillScreen + header + content + bottom
//     nav) happens only on first draw / screen change / page change; new
//     poll data repaints just the content area; a minute rollover or
//     health change repaints just the header text; the Flight countdown
//     tick repaints just its own zone (via a sprite).
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
#include "i18n.h"
#include "lcars_theme.h"
#include "lookup_budget.h"
#include "lookup_store.h"
#include "opensky_client.h"
#include "radar_view.h"
#include "redraw_policy.h"
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
static float lastRadiusDeg = 2.5f;     // config_store's default — updated every poll

// Whole config, loaded once at boot and refreshed after every poll that
// actually ran (review notes 4.3) — loop() and openSkyClientPoll() read
// this instead of hitting NVS every iteration. A config_portal save is
// picked up within one poll interval.
static Config cachedCfg;

// lastScreenChangeMs is the auto-cycle timer base, reset by
// onScreenChanged() on every screen change — manual or auto.
static uint32_t lastScreenChangeMs = 0;
static int currentPage = 0;

// Everything that happened since the last redraw — turned into a region
// mask by computeRedrawMask() at the end of loop(), then cleared.
static RedrawEvents pendingRedraw;

// last_screen NVS-write debouncing (review notes 5.9): what we last wrote,
// and when — so an auto-cycle firing every few seconds doesn't rewrite the
// same key thousands of times a day.
static int lastPersistedScreen = 0;
static uint32_t lastScreenPersistMs = 0;

// OpenSky feed health from the most recent poll — drives the header block colour
// and whether a failed poll is allowed to blank the aircraft list (it
// isn't — review notes 1.2).
static OpenSkyHealth lastHealth = OpenSkyHealth::Ok;

// True once at least one poll succeeded — until then the header block shows
// "--" rather than claiming "0 flights".
static bool hasAircraftData = false;

// Closest-point-of-approach for the nearest aircraft, as computed at the
// last poll, plus the millis() timestamp of that poll — currentCpa()
// extrapolates between polls so the Flight screen's countdown ticks every
// second (CLAUDE.md "local ticking between polls").
static CpaPrediction lastPolledCpa;
// Which of lastRows Screen 2 shows (pickNowAircraft()), -1 = none.
static int nowRowIndex = -1;
static uint32_t lastCpaPollMs = 0;

// Height of the table's page-back / page-forward tap strips — table_view's
// own row height, so the two can't drift apart (review notes 5.5).
static const int16_t kPageTapZoneHeight = tableRowHeightPx();

// Nearest aircraft's CPA, extrapolated from the last poll to "now"
// (cpa_predictor owns the math — review notes 3.4).
static CpaPrediction currentCpa() {
  return extrapolateCpa(lastPolledCpa, millis() - lastCpaPollMs);
}

// The active screen's content (y:28..225), drawn over whatever is there —
// callers clear first.
static void drawActiveContent() {
  int16_t screenW = static_cast<int16_t>(tft.width());

  switch (nav.current()) {
    case kScreenFlight: {
      static const AircraftRow kNoRow{};
      const bool hasNow = nowRowIndex >= 0 && nowRowIndex < static_cast<int>(lastRows.size());
      drawFlightScreen(tft, hasNow ? lastRows[static_cast<size_t>(nowRowIndex)] : kNoRow, hasNow,
                       currentCpa(), screenW, lastHealth);
      break;
    }
    case kScreenRadar:
      drawRadarView(tft, lastRows, lastRadiusDeg, screenW, lastHealth);
      break;
    case kScreenFlights:
    default:
      drawTablePage(tft, lastRows, screenW, currentPage, lastHealth);
      break;
  }
}

// Header bar: aircraft-count block (coloured by feed health) + date/time.
static void drawHeader() {
  drawStatusBar(tft, static_cast<int>(lastRows.size()), hasAircraftData, lastHealth,
                timeSyncNowLocal(), isTimeSynced(), static_cast<int16_t>(tft.width()));
}

// Repaints exactly the regions in `mask` (redraw_policy bits).
static void applyRedraw(uint8_t mask) {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  if (mask & kRedrawFull) {
    // Structural change: clear everything, then header, content, nav bar.
    tft.fillScreen(LCARS_BLACK);
    drawHeader();
    drawActiveContent();
    drawBottomNav(tft, nav.current(), screenW, screenH, statusBarScreenName);
    return;
  }

  if (mask & kRedrawContent) {
    // Only the band between header and bottom nav; chrome is left alone.
    const int16_t contentBottom = static_cast<int16_t>(screenH - LCARS_BOTTOM_NAV_HEIGHT);
    tft.fillRect(0, LCARS_HEADER_HEIGHT, screenW,
                 static_cast<int16_t>(contentBottom - LCARS_HEADER_HEIGHT), LCARS_BLACK);
    drawActiveContent();
  }

  if (mask & kRedrawHeader) {
    drawHeader();
  }

  if (mask & kRedrawCountdown) {
    drawFlightCountdown(tft, !lastRows.empty(), currentCpa(), screenW, lastHealth);
  }
}

// Shown instead of a screen while config_store's home_configured is still
// false (review notes 1.5/1.6). Redrawn on every full-redraw event (cheap:
// a fill + two strings), so a stray touch can't permanently dismiss it the
// way the old draw-once guard let it.
static void showSetupPrompt() {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  tft.fillScreen(LCARS_BLACK);
  tft.setFont(LCARS_FONT_BODY);
  tft.setTextDatum(middle_center);
  tft.setTextColor(THEME_CHROME_ACCENT, LCARS_BLACK);
  tft.drawString(tr(Str::SetupPrompt), screenW / 2, screenH / 2 - 10);
  tft.drawString("http://cyd-sky.local", screenW / 2, screenH / 2 + 10);
}

// Re-reads the config from NVS (after a config_portal save, or at a poll)
// and applies what affects the display right away: the language and the
// setup-prompt <-> real-screens switch both need a full repaint.
static void reloadConfig() {
  const bool wasHomeConfigured = cachedCfg.home_configured;
  const Language previousLanguage = cachedCfg.language;

  cachedCfg = loadConfig();
  lastRadiusDeg = cachedCfg.radius_deg;

  if (cachedCfg.home_configured != wasHomeConfigured) pendingRedraw.firstDraw = true;
  if (cachedCfg.language != previousLanguage) {
    i18nSetLanguage(cachedCfg.language);
    pendingRedraw.languageChanged = true;
  }
}

// Persist (debounced) + redraw after screen_nav reports the active screen
// changed. `userInitiated` is true for a touch, false for an auto-cycle
// advance — auto-cycle screen writes are rate-limited so a short cycle
// interval doesn't wear the flash (review notes 5.9).
static void onScreenChanged(bool userInitiated) {
  currentPage = 0;
  lastScreenChangeMs = millis();  // any change resets the auto-cycle timer

  if (nav.current() != lastPersistedScreen &&
      shouldPersistScreenChange(userInitiated, millis() - lastScreenPersistMs,
                                kScreenPersistMinIntervalMs)) {
    saveLastScreen(nav.current());  // targeted single-key write (see config_store)
    lastPersistedScreen = nav.current();
    lastScreenPersistMs = millis();
  }

  pendingRedraw.screenChanged = true;
}

// Dispatches one touch tap (edge-detected in loop(), once per press).
static void handleTap(int16_t x, int16_t y) {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  // Screens switch only via the bottom nav buttons.
  NavHit hit = navHitTest(x, y, screenW, screenH);
  if (hit.action == NavAction::JumpTo) {
    if (nav.set(hit.target)) onScreenChanged(/*userInitiated=*/true);
    return;
  }

  // Anywhere else: only the Flights table reacts (page the rows).
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
    pendingRedraw.pageChanged = true;
  } else if (hitTest(x, y, nextZone) && currentPage < pageCount - 1) {
    currentPage++;
    pendingRedraw.pageChanged = true;
  }
}

// --- /screenshot.bmp support (config_portal streams, we read pixels) ---

// ?screen=N: switch to that screen and repaint it right now, so the
// capture shows it (same path as a tap on its nav button).
static void screenshotPrepare(int screenIndex) {
  if (nav.set(screenIndex)) onScreenChanged(/*userInitiated=*/true);
  if (cachedCfg.home_configured) {
    applyRedraw(computeRedrawMask(pendingRedraw, nav.current()));
  }
  pendingRedraw = RedrawEvents{};
}

// One screen row read back from the panel (LovyanGFX readRect) as B,G,R.
static void screenshotReadRow(int y, uint8_t *bgr) {
  static lgfx::rgb888_t line[320];
  const int w = static_cast<int>(tft.width());
  tft.readRect(0, y, w, 1, line);
  for (int x = 0; x < w; ++x) {
    bgr[3 * x + 0] = line[x].b;
    bgr[3 * x + 1] = line[x].g;
    bgr[3 * x + 2] = line[x].r;
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();  // also brings up the XPT2046 touch controller (LGFX_CYD.hpp)
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(LCARS_BLACK);

  cachedCfg = loadConfig();
  i18nSetLanguage(cachedCfg.language);  // before anything is drawn
  nav.set(cachedCfg.last_screen);  // restore the last-active screen
  lastPersistedScreen = nav.current();
  lastRadiusDeg = cachedCfg.radius_deg;
  lastScreenChangeMs = millis();
  lastScreenPersistMs = millis();

  wifiManagerBegin();  // tries the saved network; falls back to its own captive portal

  timeSyncBegin();  // NTP (Europe/Warsaw), after WiFi; header shows --:-- until it lands

  // Starts mDNS + the config WebServer right away, non-blocking. In the
  // common case (a saved network that's reachable) wifiManagerBegin()
  // above has already connected by the time we get here; in the portal
  // fallback case cyd-sky.local won't resolve until the device later joins
  // a real network — the captive portal covers initial setup then.
  configPortalBegin();

  ScreenshotSource shot;
  shot.width = static_cast<int>(tft.width());
  shot.height = static_cast<int>(tft.height());
  shot.screenCount = kScreenCount;
  shot.prepare = screenshotPrepare;
  shot.readRow = screenshotReadRow;
  configPortalSetScreenshotSource(shot);

  pendingRedraw.firstDraw = true;  // paint the (empty) active screen + chrome immediately
}

void loop() {
  wifiManagerLoop();
  configPortalLoop();

  // --- Settings saved on the config page: apply them now ---------------
  static uint32_t lastSeenSaveCount = 0;
  if (configPortalSaveCount() != lastSeenSaveCount) {
    lastSeenSaveCount = configPortalSaveCount();
    reloadConfig();
  }

  // --- Touch: edge-detected, one handleTap() per press --------------------
  static bool wasTouchPressed = false;
  TouchPoint touch = touchInputRead(tft);
  if (touch.pressed && !wasTouchPressed) {
    // One line per tap: raw XPT2046 values + where they mapped to — what
    // touch calibration is tuned from (include/touch_calibration.h).
    Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d) nav=%d\n", touch.raw_x, touch.raw_y,
                  touch.x, touch.y,
                  static_cast<int>(navHitTest(touch.x, touch.y, static_cast<int16_t>(tft.width()),
                                              static_cast<int16_t>(tft.height()))
                                       .action));
    handleTap(touch.x, touch.y);
  }
  wasTouchPressed = touch.pressed;

  // --- OpenSky polling (only meaningful once connected) -------------------
  if (wifiManagerStatus() == WifiStatus::Connected) {
    OpenSkyPollResult poll = openSkyClientPoll(millis(), cachedCfg);

    if (poll.status != OpenSkyPollStatus::NotDue) {
      // A poll actually ran — refresh the cached config (config_portal
      // saves are also picked up immediately above; this is the backstop)
      // and update the feed health (header block colour).
      reloadConfig();

      const OpenSkyHealth previousHealth = lastHealth;
      switch (poll.status) {
        case OpenSkyPollStatus::Ok:
          lastHealth = OpenSkyHealth::Ok;
          break;
        case OpenSkyPollStatus::RateLimited:
          lastHealth = OpenSkyHealth::RateLimited;
          break;
        case OpenSkyPollStatus::AuthError:
          lastHealth = OpenSkyHealth::AuthError;
          break;
        default:
          lastHealth = OpenSkyHealth::NetworkError;
          break;
      }
      if (lastHealth != previousHealth) {
        pendingRedraw.healthChanged = true;
        // An empty screen says why it is empty (emptyStateText()), so it has
        // to be repainted when the reason changes, not only the header dot.
        if (lastRows.empty()) pendingRedraw.newPoll = true;
      }

      Serial.printf("[main] poll status=%d aircraft=%u home_configured=%d free_heap=%u\n",
                    static_cast<int>(poll.status), static_cast<unsigned>(poll.aircraft.size()),
                    cachedCfg.home_configured ? 1 : 0, static_cast<unsigned>(ESP.getFreeHeap()));

      // Only a clean poll updates the aircraft list — a failure keeps the
      // last good data on screen rather than blanking it (review notes 1.2).
      if (poll.status == OpenSkyPollStatus::Ok && cachedCfg.home_configured) {
        const std::vector<Aircraft> &aircraft = poll.aircraft;

        // Rank aircraft (airborne nearest-first, then ground) so the limited per-poll lookup
        // budget is spent on the ones the user actually sees first.
        lastRows = buildEnrichedRecords(aircraft, {}, {});
        annotateDistances(lastRows, cachedCfg.home_lat, cachedCfg.home_lon);
        sortRowsForDisplay(lastRows);  // airborne first, then ground traffic (#9)

        std::vector<LookupKeys> byPriority;
        byPriority.reserve(lastRows.size());
        for (const AircraftRow &r : lastRows) {
          byPriority.push_back({r.aircraft.icao24, r.aircraft.callsign});
        }
        // "Cached" = in RAM or persisted in NVS (lookup_store) — neither costs
        // a hexdb.io request, so neither should use up the per-poll budget.
        auto aircraftKnown = [](const std::string &k) {
          return aircraftLookupIsCached(k) || storeHasAircraft(k);
        };
        auto routeKnown = [](const std::string &k) {
          return routeLookupIsCached(k) || storeHasRoute(k);
        };
        auto airportKnown = [](const std::string &k) {
          return airportLookupIsCached(k) || storeHasAirport(k);
        };
        LookupPlan plan = planLookups(byPriority, aircraftKnown, routeKnown, kDefaultLookupBudget);

        std::map<std::string, AircraftInfo> infoByIcao24;
        for (const std::string &hex : plan.icao24) {
          infoByIcao24[hex] = lookupAircraft(hex);
        }
        std::map<std::string, RouteInfo> routeByCallsign;
        for (const std::string &cs : plan.callsigns) {
          routeByCallsign[cs] = lookupRoute(cs);
        }

        // Fold the enrichment into the already-sorted rows in place —
        // it doesn't change distance, so no re-sort needed.
        // A key the budget didn't reach this cycle is *pending* ("..."),
        // not unknown ("--") — it'll be fetched in a later poll.
        for (AircraftRow &r : lastRows) {
          auto info = infoByIcao24.find(r.aircraft.icao24);
          if (info != infoByIcao24.end()) {
            r.info = info->second;
          } else {
            r.info_pending = !r.aircraft.icao24.empty();
          }
          auto route = routeByCallsign.find(r.aircraft.callsign);
          if (route != routeByCallsign.end()) {
            r.route = route->second;
          } else {
            r.route_pending = !r.aircraft.callsign.empty();
          }
        }
        classifyPhases(lastRows, cachedCfg.near_airport_km, cachedCfg.climb_rate_threshold_mps);

        // Route-endpoint airports for *every* row with a route, so the whole
        // UI shows IATA codes. Cache hits are free; new hexdb.io lookups are
        // budgeted per poll (planLookups, airport codes in the icao24 slot),
        // nearest rows first — the rest stay pending and fill in over the
        // next polls.
        std::vector<LookupKeys> airportKeys;
        for (const AircraftRow &r : lastRows) {
          if (!r.route.found) continue;
          airportKeys.push_back({r.route.origin_icao, ""});
          airportKeys.push_back({r.route.dest_icao, ""});
        }
        LookupPlan airportPlan =
            planLookups(airportKeys, airportKnown, routeKnown, kDefaultAirportLookupBudget);
        std::map<std::string, AirportInfo> airportByCode;
        for (const std::string &code : airportPlan.icao24) {
          airportByCode[code] = lookupAirportCountry(code);
        }
        for (AircraftRow &r : lastRows) {
          if (!r.route.found) continue;
          auto o = airportByCode.find(r.route.origin_icao);
          if (o != airportByCode.end()) r.origin_airport = o->second;
          else r.origin_airport_pending = true;
          auto d = airportByCode.find(r.route.dest_icao);
          if (d != airportByCode.end()) r.dest_airport = d->second;
          else r.dest_airport_pending = true;
        }

        // Screen 2 ("TERAZ" / "NOW"): predict every aircraft's closest point of
        // approach and show the one that will be overhead soonest (not just
        // the nearest). currentCpa() ticks its countdown between polls.
        std::vector<CpaPrediction> cpas(lastRows.size());
        for (size_t i = 0; i < lastRows.size(); ++i) {
          const Aircraft &ac = lastRows[i].aircraft;
          if (!ac.has_position) continue;
          cpas[i] = predictCpa(cachedCfg.home_lat, cachedCfg.home_lon, ac.lat, ac.lon,
                               ac.velocity, ac.true_track);
        }
        const NowPick now = pickNowAircraft(lastRows, cpas, kOverheadRadiusKm);
        nowRowIndex = now.index;
        lastPolledCpa = now.cpa;
        lastCpaPollMs = millis();

        FeaturedSplit split = splitFeaturedAndRest(lastRows);
        int pageCount = getPageCount(static_cast<int>(split.rest.size()), tableRowsPerPage());
        if (currentPage >= pageCount) {
          currentPage = pageCount > 0 ? pageCount - 1 : 0;
        }

        hasAircraftData = true;
        pendingRedraw.newPoll = true;  // content + header count; bottom nav untouched
      }
    }
  }

  // --- Flight screen: tick the countdown ~1 Hz between polls --------------
  static uint32_t lastFlightTickMs = 0;
  if (nav.current() == kScreenFlight && (millis() - lastFlightTickMs) >= 1000) {
    lastFlightTickMs = millis();
    pendingRedraw.countdownTick = true;
  }

  // --- Header clock: repaint its text when HH:MM rolls over -------------
  // -1 while unsynced, so the first NTP sync also counts as a change.
  static long lastDrawnMinute = -2;
  const long minuteNow = isTimeSynced() ? minuteOf(timeSyncNowLocal()) : -1;
  if (minuteNow != lastDrawnMinute) {
    lastDrawnMinute = minuteNow;
    pendingRedraw.minuteChanged = true;
  }

  // --- Auto screen cycling (CLAUDE.md "Auto screen cycling") ------------
  if (cachedCfg.auto_cycle_enabled && cachedCfg.home_configured) {
    CpaPrediction cpa = currentCpa();
    bool deferHold = shouldDeferAutoSwitch(nav.current(), cpa.found, cpa.t_cpa_seconds);
    if (shouldAutoAdvance(millis() - lastScreenChangeMs, cachedCfg.auto_cycle_interval_s,
                          deferHold)) {
      nav.next();                        // 3 distinct screens, wraps — always a real change
      onScreenChanged(/*userInitiated=*/false);  // resets the timer + page, forces redraw
    }
  }

  // --- Redraw only what changed (CLAUDE.md "Redraw policy") -------------
  const uint8_t mask = computeRedrawMask(pendingRedraw, nav.current());
  if (mask != 0) {
    if (cachedCfg.home_configured) {
      applyRedraw(mask);
    } else if (mask & kRedrawFull) {
      showSetupPrompt();  // "set your home location" until config_portal is used
    }
  }
  pendingRedraw = RedrawEvents{};
}
