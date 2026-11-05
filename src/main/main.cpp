// CYD Sky Tracker — main orchestration: the UI on core 1.
//
// Two cores, two jobs (spec/done/04-dual-core.md): net_task on core 0 does all
// the networking — WiFi, the config web page, OpenSky, hexdb.io, GitHub, OTA
// — and publishes what the screens need as a NetState snapshot (core_link).
// This file is loop() on core 1: the display, touch and everything drawn.
// It never waits for the network, and the network never draws.
//
// setup(): initializes LovyanGFX (configured via display/adapters/lgfx/LGFX_CYD.hpp) and
// its built-in XPT2046 touch driver (read via touch_xpt2046), the backlight,
// loads the persisted config (config_store) — restoring which of the three
// screens was last active (last_screen 0/1/2) into screen_nav — offers the
// forgotten-password reset, paints the first frame (before WiFi is up), then
// starts net_task.
//
// loop(): one pass, then sleep until the nearest deadline (touch poll, the
// countdown second, the clock minute) or until net_task publishes.
//   - New NetState: diffNetState() turns it into redraw events (new poll,
//     health, language, home location). A failed poll keeps the last good
//     list (the same DataSnapshot). No heartbeat from net_task for 60 s ->
//     shown as a network error rather than pretending stale data is fresh.
//   - Touch (edge-detected, one action per press): screen_nav::navHitTest
//     classifies the tap — only a bottom-nav button switches screens; every
//     screen change persists to config_store (last_screen) and forces a
//     redraw. Otherwise, on the Flights screen, a tap in the top/bottom row
//     strip pages the table (swap which 5 rows show — never scroll).
//   - The Flight screen's countdown ticks locally every second between
//     polls: currentCpa() extrapolates the snapshot's CPA by the elapsed
//     millis() — repainting only the countdown zone, never the whole screen.
//   - Auto screen cycling (off unless config_store's auto_cycle_enabled):
//     screen_nav::shouldAutoAdvance() / shouldDeferAutoSwitch(), every
//     screen change resets the timer.
//   - Firmware updates (spec/01, spec/02): net_task reports progress in
//     NetState::ota and this draws service_screen from it. The update
//     question ("new version — install it?") is asked here; YES / NO go to
//     net_task as commands.
//   - Holding a finger on the screen at power-up offers to remove a
//     forgotten web panel password (web_auth, service_screen).
//   - Device alerts (spec 08): net_task derives them (device_alerts) into
//     NetState::alerts; the header shows a "!" and tapping it opens the
//     message screen (service_screen's AlertScreen) for up to 15 s.
//   - Brightness (spec/03): backlight follows the light sensor and the night
//     mode; the "pause polling at night" decision goes to net_task as an
//     atomic flag.
//   - /screenshot.bmp: the web handler (core 0) asks for bands of rows; this
//     loop reads them off the panel (the display belongs to this core).
//   - Redraw (only what changed — CLAUDE.md "Redraw policy"): loop()
//     collects RedrawEvents and redraw_policy::computeRedrawMask() picks the
//     regions.
//
// This file is deliberately thin: all actual logic lives in the modules
// above — see CLAUDE.md "Testing" for why only this kind of orchestration
// code is allowed to go untested by Unity.

#include <Arduino.h>
#include <esp_ota_ops.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "display/adapters/backlight_ldr.h"
#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/adapters/lgfx/flight_screen_draw.h"
#include "display/adapters/lgfx/lcars_theme_draw.h"
#include "display/adapters/lgfx/radar_view_draw.h"
#include "display/adapters/lgfx/screenshot_panel.h"
#include "display/adapters/lgfx/service_screen_draw.h"
#include "display/adapters/lgfx/status_bar_draw.h"
#include "display/adapters/lgfx/table_view_draw.h"
#include "display/adapters/service_flows.h"
#include "display/adapters/touch_xpt2046.h"
#include "display/core/backlight.h"
#include "admin/adapters/web_server.h"
#include "admin/core/config_store.h"
#include "platform/core_link.h"
#include "airspace/core/cpa_predictor.h"
#include "admin/core/device_alerts.h"
#include "display/core/screens/flight_screen.h"
#include "display/core/i18n.h"
#include "display/core/theme/lcars_theme.h"
#include "main/net_task.h"
#include "admin/core/firmware/ota_update.h"
#include "display/core/screens/radar_view.h"
#include "display/core/redraw_policy.h"
#include "admin/core/firmware/release_check.h"
#include "display/core/screen_nav.h"
#include "display/services/ui_flow.h"
#include "display/services/ui_perf.h"
#include "display/core/screens/service_screen.h"
#include "display/core/screens/status_bar.h"
#include "display/core/screens/table_view.h"
#include "platform/time_sync.h"
#include "shared/clock.h"  // minuteOf()
#include "display/core/touch_input.h"
#include "admin/core/firmware/version.h"
#include "admin/core/web_auth.h"

// No SET_LOOP_TASK_STACK_SIZE: the TLS handshakes that needed 16 KB run in
// net_task now; drawing fits the core's default 8 KB (checked in [perf]).

static LGFX tft;

// The active screen (0 Flights / 1 Flight / 2 Radar). Seeded from
// config_store's last_screen at boot; current() is re-persisted whenever a
// touch changes it.
static ScreenNav nav;

// The latest snapshot from net_task, and its sequence number in the box.
static std::shared_ptr<const NetState> net;
static uint32_t netSeen = 0;

// What a screen change sets off (persist, page, the auto-cycle timer), the
// Flights table's page, the header clock's minute, the NOW countdown's tick.
static ScreenChangeTracker screenChanges;
static TablePager pager;
static MinuteWatch minuteWatch;
static CountdownTick countdownTick;

// Everything that happened since the last redraw — turned into a region
// mask by computeRedrawMask() at the end of loop(), then cleared.
static RedrawEvents pendingRedraw;

// The health the header shows: net_task's, or NetworkError once its
// heartbeat is stale (core_link::heartbeatStale()).
static OpenSkyHealth shownHealth = OpenSkyHealth::Ok;

static const Config &cfg() { return *net->cfg; }

static const std::vector<AircraftRow> &rows() {
  static const std::vector<AircraftRow> kNone;
  return net->data != nullptr ? net->data->rows : kNone;
}

// Screen 2's aircraft's CPA, extrapolated from the poll to "now"
// (cpa_predictor owns the math — docs/review-notes-2026.md 3.4).
static CpaPrediction currentCpa() {
  if (net->data == nullptr) return CpaPrediction{};
  return extrapolateCpa(net->data->cpa, millis() - net->data->polledAtMs);
}

// The active screen's content, drawn over whatever is there — callers clear first.
static void drawActiveContent() {
  int16_t screenW = static_cast<int16_t>(tft.width());

  switch (nav.current()) {
    case kScreenFlight: {
      static const AircraftRow kNoRow{};
      const int idx = net->data != nullptr ? net->data->nowRowIndex : -1;
      const bool hasNow = idx >= 0 && idx < static_cast<int>(rows().size());
      drawFlightScreen(tft, hasNow ? rows()[static_cast<size_t>(idx)] : kNoRow, hasNow,
                       currentCpa(), screenW, shownHealth);
      break;
    }
    case kScreenRadar:
      drawRadarView(tft, rows(), cfg().radius_deg, screenW, shownHealth);
      break;
    case kScreenFlights:
    default:
      drawTablePage(tft, rows(), screenW, pager.page(), shownHealth);
      break;
  }
}

// Header bar: aircraft count + health dot + device-alert "!" + date/time.
static void drawHeader() {
  drawStatusBar(tft, static_cast<int>(rows().size()), net->data != nullptr, shownHealth,
                net->alerts.displaySeverity(), timeSyncNowLocal(), isTimeSynced(),
                static_cast<int16_t>(tft.width()));
}

// The header's layout as drawn now — where the "!" badge sits (alert_x).
static HeaderZones headerZones() {
  const bool synced = isTimeSynced();
  const std::time_t local = timeSyncNowLocal();
  return statusBarZones(
      static_cast<int>(tft.width()), kStatusBarAppName,
      statusBarBlockLabel(static_cast<int>(rows().size()), net->data != nullptr, i18nLanguage()),
      statusBarDate(local, synced), statusBarClock(local, synced),
      net->alerts.displaySeverity() != AlertSeverity::None);
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
    drawFlightCountdown(tft, !rows().empty(), currentCpa(), screenW, shownHealth);
  }
}

// Shown instead of a screen while config_store's home_configured is still
// false (docs/review-notes-2026.md 1.5/1.6). Redrawn on every full-redraw event.
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

// A new snapshot from net_task (if there is one) -> redraw events.
static void takeNetState() {
  std::shared_ptr<const NetState> next;
  if (!core_link::netState.takeIfNewer(netSeen, next)) return;
  const RedrawEvents e = diffNetState(net.get(), *next);
  if (next->cfg->language != net->cfg->language) i18nSetLanguage(next->cfg->language);
  net = std::move(next);
  pendingRedraw.firstDraw |= e.firstDraw;
  pendingRedraw.languageChanged |= e.languageChanged;
  pendingRedraw.newPoll |= e.newPoll;
  pendingRedraw.minuteChanged |= e.minuteChanged;
  pendingRedraw.alertsChanged |= e.alertsChanged;
  if (e.newPoll) pager.clamp(tablePageCount(rows()));  // a shorter list: its last page
}

// The header's health: net_task's, unless it stopped answering altogether.
static void updateShownHealth() {
  const ShownHealthStep h = shownHealthStep(shownHealth, net->health, millis(),
                                            core_link::netBeatMs.load(), rows().empty());
  if (!h.healthChanged) return;
  if (h.stalled) Serial.println("[main] network task silent for 60 s");
  shownHealth = h.shown;
  pendingRedraw.healthChanged = true;
  if (h.newPoll) pendingRedraw.newPoll = true;
}

// After screen_nav reports the active screen changed: page, auto-cycle timer,
// redraw, and last_screen saved when screenChanges says so.
static void onScreenChanged(ScreenChangeCause cause) {
  if (screenChanges.onChange(nav.current(), cause, millis(), pager, pendingRedraw).persist) {
    saveLastScreen(nav.current());  // targeted single-key write (see config_store)
  }
}

// --- Device alerts: the message screen behind the header's "!" (spec 08) -----

static AlertScreenFlow alertScreen;
static TouchEdge touchEdge;  // one tap per press

// Back to the normal screens: same screen and table page, a full repaint, a
// fresh auto-cycle interval.
static void closeAlertMessage() {
  alertScreen.closeQuietly();
  screenChanges.restartAutoCycle(millis());
  pendingRedraw.firstDraw = true;
}

// One tap (edge-detected in loop(), once per press): routeTap() says what it
// is, this does it.
static void onTap(int16_t x, int16_t y) {
  TapContext c;
  c.currentScreen = nav.current();
  c.page = pager.page();
  c.pageCount = tablePageCount(rows());
  c.homeConfigured = cfg().home_configured;
  c.header = headerZones();
  c.screenW = static_cast<int16_t>(tft.width());
  c.screenH = static_cast<int16_t>(tft.height());
  const TapAction a = routeTap(x, y, c);
  switch (a.kind) {
    case TapKind::OpenAlerts:
      showAlertScreen(tft, alertScreen, net->alerts, net->alertDetails);
      break;
    case TapKind::JumpTo:
      if (nav.set(a.target)) onScreenChanged(ScreenChangeCause::Touch);
      break;
    case TapKind::PagePrev:
      if (pager.prev()) pendingRedraw.pageChanged = true;
      break;
    case TapKind::PageNext:
      if (pager.next(c.pageCount)) pendingRedraw.pageChanged = true;
      break;
    case TapKind::None:
    default:
      break;
  }
}

// --- Firmware update: drawn from NetState::ota ------------------------------

// The arduino-esp32 core marks a freshly updated image valid right at boot
// unless this says "later" — net_task does it once the board has proven it
// runs (ota_update::shouldMarkAppValid(), including "this UI loop is alive").
extern "C" bool verifyRollbackLater() { return true; }

static OtaScreenFlow otaScreen;

// True while the OTA screens own the display (running, done, a failure on show).
static bool otaScreenPass() {
  const OtaScreenStep r = runOtaScreen(tft, otaScreen, net->ota, net->restartPending,
                                       firmwareVersion());
  if (r.ownsDisplay) alertScreen.closeQuietly();  // the OTA screen comes first
  if (r.backToScreens) pendingRedraw.firstDraw = true;
  return r.ownsDisplay;
}

// --- New version on GitHub: the question on the display ------------------------

// "NEW VERSION AVAILABLE — INSTALL IT?" — shown at most once per boot, over
// the normal screens, until YES / NO / 30 s (update_prompt_flow).
static UpdatePromptFlow prompt;

static void maybeAskAboutUpdate(bool touchPressed) {
  const UpdateSummary &u = net->update;
  PromptGate g;
  g.status = u.status;
  g.latestTag = u.latestTag;
  g.skippedTag = cfg().update_skipped_tag;
  g.installedIsDev = isDevelopmentBuild(parseFwVersion(u.installed));
  g.homeConfigured = cfg().home_configured;
  g.currentScreen = nav.current();
  const CpaPrediction cpa = currentCpa();
  g.cpaFound = cpa.found;
  g.tCpaSeconds = static_cast<int>(cpa.t_cpa_seconds);
  g.touchActive = touchPressed;
  g.screenOff = backlightDecision().screen_off;
  g.alertScreenOpen = alertScreen.isOpen();
  if (prompt.tryOpen(g, u.latestTag, millis())) openUpdatePrompt(tft, prompt, u.installed);
}

// YES / NO go to net_task as a NetCommand. If the queue is full the command
// waits here and is retried on every loop() pass until it fits — the answer
// must not get lost (spec 09 §2).
static PendingCommand pendingCmd;

// While the prompt is up it owns the touchscreen.
static void updatePromptPass() {
  const PromptStep r = runUpdatePrompt(tft, prompt);
  if (r.command.type != NetCommandType::None) {
    pendingCmd = submitCommand(r.command, core_link::sendCommand);
    if (pendingCmd.logQueueFull) Serial.println("[update] command queue full, retrying");
  }
  if (r.firstDraw) pendingRedraw.firstDraw = true;
}

// --- /screenshot.bmp: the web handler (core 0) asks, this loop reads -------
// (display/adapters/lgfx/screenshot_panel; the hand-over is screenshot_bridge).

// ?screen=N: switch to that screen and repaint it right now, so the capture
// shows it — or draw one of the two demo screens over it.
static void prepareCapture(int screenIndex) {
  const ScreenshotPrepare p = screenshotPrepareTarget(screenIndex, kScreenCount);
  if (p.kind == ScreenshotPrepareKind::UpdatePromptDemo ||
      p.kind == ScreenshotPrepareKind::AlertDemo) {
    drawScreenshotDemo(tft, p.kind);
    alertScreen.closeQuietly();
    pendingRedraw = RedrawEvents{};
    pendingRedraw.firstDraw = true;  // repaints the real screen once the capture is out
    return;
  }
  if (alertScreen.isOpen()) closeAlertMessage();  // a normal screen was asked for
  if (p.kind == ScreenshotPrepareKind::Screen && nav.set(p.screen)) {
    onScreenChanged(ScreenChangeCause::Screenshot);
  }
  const uint8_t mask = computeRedrawMask(pendingRedraw, nav.current());
  if (mask != 0 && cfg().home_configured) applyRedraw(mask);
  pendingRedraw = RedrawEvents{};
}

// True while a capture runs. When it is over (end(), timeout or the 5 s idle
// rule), the screen shown before the capture comes back — repainted, not saved.
static bool screenshotPass() {
  const ScreenshotPassResult r =
      screenshotPanelPass(tft, nav.current(), kScreenCount, prepareCapture);
  if (r.backTo >= 0 && nav.set(r.backTo)) onScreenChanged(ScreenChangeCause::Screenshot);
  return r.capturing;
}

// --- Perf: UI loop gaps (ui_perf), handed to net_task once a minute -----------

static UiPerfMeter perfMeter;

static void perfLoopStart() {
  if (!perfMeter.tick(millis())) return;
  UiPerfShare share = perfMeter.share();
  share.stackFree = uxTaskGetStackHighWaterMark(nullptr);
  core_link::uiPerf.set(share);
}

// --- Sleep until the next thing to do --------------------------------------------

static void sleepUntilNextDeadline() {
  const uint32_t now = millis();
  UiWake w;
  if (nav.current() == kScreenFlight) {
    w.untilCountdownMs = countdownTick.msUntilNext(now);
  }
  if (isTimeSynced()) {
    const std::time_t local = timeSyncNowLocal();
    w.untilMinuteMs = msUntilNextMinute(static_cast<int>(local % 60), 0);
  }
  w.untilPromptTimerMs = prompt.msUntilTimer(now);
  if (alertScreen.isOpen()) w.untilAlertTimerMs = alertScreen.msToNextSecond(now);
  const uint32_t sleepMs = uiSleepMs(w);
  perfMeter.setPlannedSleep(sleepMs);
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepMs));
}

void setup() {
  Serial.begin(115200);

  tft.init();  // also brings up the XPT2046 touch controller (LGFX_CYD.hpp)
  backlightBegin(tft);  // full brightness until the light sensor has settled
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(LCARS_BLACK);

  Config bootCfg = loadConfig();
  i18nSetLanguage(bootCfg.language);  // before anything is drawn

  Serial.printf("[ota] %s\n", firmwareMarker());
  bool appPendingVerify = false;
  {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    appPendingVerify = running != nullptr &&
                       esp_ota_get_state_partition(running, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_PENDING_VERIFY;
    Serial.printf("[ota] running from %s, %s\n", running != nullptr ? running->label : "?",
                  appPendingVerify ? "pending verify (rollback armed)" : "valid");
  }

  // The NOW screen's countdown sprite (~30 KB), before WiFi and net_task start
  // allocating — created later it split the largest free block (spec/04 §10).
  flightScreenReserveSprite(static_cast<int16_t>(tft.width()));

  // A forgotten panel password: a finger on the screen at power-up (before
  // net_task exists — the blocking delay() steps are fine here).
  runPasswordReset(tft, !bootCfg.web_password.empty(), [&bootCfg] {
    bootCfg.web_password.clear();
    saveConfig(bootCfg);
    bootCfg = loadConfig();
    Serial.println("[auth] web password cleared from the touchscreen");
  });
  nav.set(bootCfg.last_screen);    // restore the last-active screen
  screenChanges.begin(nav.current(), millis());

  // The first snapshot: the settings, no data yet — the chrome and "--" go on
  // screen right away, before WiFi is even up.
  auto cfgPtr = std::make_shared<const Config>(bootCfg);
  auto first = std::make_shared<NetState>();
  first->cfg = cfgPtr;
  net = first;

  ScreenshotSource shotSrc;
  shotSrc.width = static_cast<int>(tft.width());
  shotSrc.height = static_cast<int>(tft.height());
  shotSrc.screenCount = kScreenCount + 2;  // + the update prompt and alert screen demos
  shotSrc.begin = screenshotPanelBegin;
  shotSrc.requestBand = screenshotPanelRequestBand;
  shotSrc.waitBand = screenshotPanelWaitBand;
  shotSrc.end = screenshotPanelEnd;
  configPortalSetScreenshotSource(shotSrc);

  core_link::begin(xTaskGetCurrentTaskHandle());
  core_link::uiBeatMs.store(millis());
  core_link::netBeatMs.store(millis());
  netTaskStart(cfgPtr, appPendingVerify);

  pendingRedraw.firstDraw = true;  // paint the (empty) active screen + chrome immediately
}

void loop() {
  perfLoopStart();
  core_link::uiBeatMs.store(millis());
  pendingCmd = retryPendingCommand(pendingCmd, core_link::sendCommand);

  takeNetState();
  updateShownHealth();

  // Backlight: sensor, night mode, ramps. Service screens get full brightness.
  backlightLoop(tft, cfg(),
                prompt.active() || otaScreen.holdsBacklight(net->ota, net->restartPending));
  core_link::pollingAllowed.store(
      shouldPollOpenSky(backlightDecision(), cfg().night_pause_polling));

  // --- Firmware update in progress (or just failed): its screen only ---------
  if (otaScreenPass()) {
    pendingRedraw = RedrawEvents{};
    sleepUntilNextDeadline();
    return;
  }

  // --- A screenshot is being taken: hold still ---------------------------------
  if (screenshotPass()) {
    sleepUntilNextDeadline();
    return;
  }

  // --- The update question owns the screen until it's answered ----------
  if (prompt.active() && net->update.installPending) {
    prompt.supersededByInstall();  // installed from the settings page: the OTA screen follows
  }
  if (prompt.active()) {
    updatePromptPass();
    if (prompt.active()) {
      sleepUntilNextDeadline();
      return;
    }
  }

  // --- The alert message screen owns the screen while it's open --------------
  if (alertScreen.isOpen()) {
    const AlertFlowStep r = runAlertScreen(tft, alertScreen, net->alerts, net->alertDetails);
    if (r.swallowTouch) touchEdge.swallow();  // its finger, the CLOSE tap too, is no tap below
    if (r.close) closeAlertMessage();
    if (alertScreen.isOpen()) {
      pendingRedraw = RedrawEvents{};
      sleepUntilNextDeadline();
      return;
    }
  }

  // --- Touch: edge-detected, one onTap() per press ---------------------------
  TouchPoint touch = touchInputRead(tft);
  if (touchEdge.tap(touch.pressed)) {
    // One line per tap: raw XPT2046 values + where they mapped to — what
    // touch calibration is tuned from (display/core/touch_calibration.h).
    Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d) nav=%d\n", touch.raw_x, touch.raw_y,
                  touch.x, touch.y,
                  static_cast<int>(navHitTest(touch.x, touch.y, static_cast<int16_t>(tft.width()),
                                              static_cast<int16_t>(tft.height()))
                                       .action));
    if (backlightHandleTouch(cfg())) {
      screenChanges.restartAutoCycle(millis());  // only woke a dark screen: no tap
    } else {
      onTap(touch.x, touch.y);
    }
  }

  if (alertScreen.isOpen()) {  // the tap opened the "!" message
    pendingRedraw = RedrawEvents{};
    sleepUntilNextDeadline();
    return;
  }

  // --- New version on GitHub: ask once ------------------------------------
  if (!prompt.active() && !net->update.installPending) maybeAskAboutUpdate(touch.pressed);
  if (prompt.active()) {
    pendingRedraw = RedrawEvents{};
    sleepUntilNextDeadline();
    return;
  }

  // --- Flight screen: tick the countdown ~1 Hz between polls --------------
  if (countdownTick.due(nav.current(), millis())) pendingRedraw.countdownTick = true;

  // --- Header clock: repaint its text when HH:MM rolls over -------------
  // -1 while unsynced, so the first NTP sync also counts as a change.
  if (minuteWatch.changed(isTimeSynced() ? minuteOf(timeSyncNowLocal()) : -1)) {
    pendingRedraw.minuteChanged = true;
  }

  // --- Auto screen cycling (docs/screens.md "Auto screen cycling") ------------
  AutoCycleInput ac;
  ac.enabled = cfg().auto_cycle_enabled;
  ac.homeConfigured = cfg().home_configured;
  ac.screenOff = backlightDecision().screen_off;  // nobody sees a dark screen change
  ac.currentScreen = nav.current();
  const CpaPrediction cpa = currentCpa();
  ac.cpaFound = cpa.found;
  ac.tCpaSeconds = cpa.t_cpa_seconds;
  ac.elapsedMs = screenChanges.autoCycleElapsedMs(millis());
  ac.intervalS = cfg().auto_cycle_interval_s;
  if (shouldAutoCycleNow(ac)) {
    nav.next();                                     // 3 distinct screens, wraps
    onScreenChanged(ScreenChangeCause::AutoCycle);  // resets the timer + page, forces redraw
  }

  // --- Redraw only what changed (CLAUDE.md "Redraw policy") -------------
  const uint8_t mask = computeRedrawMask(pendingRedraw, nav.current());
  if (mask != 0) {
    if (cfg().home_configured) {
      applyRedraw(mask);
    } else if (mask & kRedrawFull) {
      showSetupPrompt();  // "set your home location" until web_server is used
    }
  }
  pendingRedraw = RedrawEvents{};

  sleepUntilNextDeadline();
}
