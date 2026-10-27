// CYD Sky Tracker — main orchestration: the UI on core 1.
//
// Two cores, two jobs (spec/done/04-dual-core.md): net_task on core 0 does all
// the networking — WiFi, the config web page, OpenSky, hexdb.io, GitHub, OTA
// — and publishes what the screens need as a NetState snapshot (core_link).
// This file is loop() on core 1: the display, touch and everything drawn.
// It never waits for the network, and the network never draws.
//
// setup(): initializes LovyanGFX (configured via include/LGFX_CYD.hpp) and
// its built-in XPT2046 touch driver (read via touch_input), the backlight,
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

#include "LGFX_CYD.hpp"
#include "backlight.h"
#include "config_portal.h"
#include "config_store.h"
#include "core_link.h"
#include "cpa_predictor.h"
#include "device_alerts.h"
#include "flight_screen.h"
#include "i18n.h"
#include "lcars_theme.h"
#include "net_task.h"
#include "ota_update.h"
#include "perf_stats.h"
#include "radar_view.h"
#include "redraw_policy.h"
#include "release_check.h"
#include "screen_nav.h"
#include "screenshot.h"
#include "service_screen.h"
#include "status_bar.h"
#include "table_view.h"
#include "time_sync.h"
#include "touch_input.h"
#include "version.h"
#include "web_auth.h"

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

// lastScreenChangeMs is the auto-cycle timer base, reset by
// onScreenChanged() on every screen change — manual or auto.
static uint32_t lastScreenChangeMs = 0;
static int currentPage = 0;

// Everything that happened since the last redraw — turned into a region
// mask by computeRedrawMask() at the end of loop(), then cleared.
static RedrawEvents pendingRedraw;

// last_screen NVS-write debouncing (docs/review-notes-2026.md 5.9).
static int lastPersistedScreen = 0;
static uint32_t lastScreenPersistMs = 0;

// The health the header shows: net_task's, or NetworkError once its
// heartbeat is stale (core_link::heartbeatStale()).
static OpenSkyHealth shownHealth = OpenSkyHealth::Ok;

// Height of the table's page-back / page-forward tap strips — table_view's
// own row height, so the two can't drift apart (docs/review-notes-2026.md 5.5).
static const int16_t kPageTapZoneHeight = tableRowHeightPx();

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
      drawTablePage(tft, rows(), screenW, currentPage, shownHealth);
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

// The table page can't point past the last page of a fresh list.
static void clampPage() {
  FeaturedSplit split = splitFeaturedAndRest(rows());
  int pageCount = getPageCount(static_cast<int>(split.rest.size()), tableRowsPerPage());
  if (currentPage >= pageCount) currentPage = pageCount > 0 ? pageCount - 1 : 0;
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
  if (e.newPoll) clampPage();
}

// The header's health: net_task's, unless it stopped answering altogether.
static void updateShownHealth() {
  const bool stalled =
      heartbeatStale(millis(), core_link::netBeatMs.load(), kNetStallLimitMs);
  const OpenSkyHealth h = stalled ? OpenSkyHealth::NetworkError : net->health;
  if (h == shownHealth) return;
  if (stalled) Serial.println("[main] network task silent for 60 s");
  shownHealth = h;
  pendingRedraw.healthChanged = true;
  // An empty screen says why it is empty (emptyStateText()).
  if (rows().empty()) pendingRedraw.newPoll = true;
}

// Persist (debounced) + redraw after screen_nav reports the active screen
// changed. A touch persists at once, an auto-cycle advance rate-limited
// (docs/review-notes-2026.md 5.9), a screenshot's switch never (spec 10).
static void onScreenChanged(ScreenChangeCause cause) {
  currentPage = 0;
  lastScreenChangeMs = millis();  // any change resets the auto-cycle timer

  if (nav.current() != lastPersistedScreen &&
      shouldPersistScreenChange(cause, millis() - lastScreenPersistMs,
                                kScreenPersistMinIntervalMs)) {
    saveLastScreen(nav.current());  // targeted single-key write (see config_store)
    lastPersistedScreen = nav.current();
    lastScreenPersistMs = millis();
  }

  pendingRedraw.screenChanged = true;
}

// --- Device alerts: the message screen behind the header's "!" (spec 08) -----

static AlertScreen alertScreen;
static AlertSet alertScreenDrawn;  // what its list shows

static void openAlertMessage() {
  alertScreen = openAlertScreen(millis());
  alertScreenDrawn = net->alerts;
  Serial.printf("[alert] message screen opened (%u alert(s))\n",
                static_cast<unsigned>(alertsInOrder(net->alerts, true).size()));
  drawAlertScreen(tft, net->alerts, net->alertDetails, millis(),
                  alertScreenSecondsLeft(alertScreen, millis()));
}

// Back to the normal screens: same screen and table page, a full repaint, a
// fresh auto-cycle interval.
static void closeAlertMessage() {
  alertScreen.open = false;
  lastScreenChangeMs = millis();
  pendingRedraw.firstDraw = true;
}

// While it's open it owns the touchscreen.
static void serviceAlertScreen() {
  const TouchPoint t = touchInputRead(tft);
  AlertScreenInput in;
  in.nowMs = millis();
  in.pressed = t.pressed;
  in.x = t.x;
  in.y = t.y;
  in.anyDisplayAlerts = net->alerts.displaySeverity() != AlertSeverity::None;
  in.screenW = static_cast<int16_t>(tft.width());
  in.screenH = static_cast<int16_t>(tft.height());
  const AlertScreenStepResult r = alertScreenStep(alertScreen, in);
  alertScreen = r.state;
  if (r.close) {
    closeAlertMessage();
    return;
  }
  if (net->alerts != alertScreenDrawn) {
    // One came or went: the list again, the countdown runs on.
    alertScreenDrawn = net->alerts;
    drawAlertScreen(tft, net->alerts, net->alertDetails, millis(),
                    alertScreenSecondsLeft(alertScreen, millis()));
  } else if (r.redrawTimer) {
    drawAlertCloseButton(tft, static_cast<int>(alertsInOrder(net->alerts, true).size()),
                         alertScreen.secondsShown);
  }
}

// Dispatches one touch tap (edge-detected in loop(), once per press).
static void handleTap(int16_t x, int16_t y) {
  int16_t screenW = static_cast<int16_t>(tft.width());
  int16_t screenH = static_cast<int16_t>(tft.height());

  // The "!" first: its zone reaches a little into the content area.
  if (cfg().home_configured && alertBadgeHit(x, y, headerZones())) {
    openAlertMessage();
    return;
  }

  // Screens switch only via the bottom nav buttons.
  NavHit hit = navHitTest(x, y, screenW, screenH);
  if (hit.action == NavAction::JumpTo) {
    if (nav.set(hit.target)) onScreenChanged(ScreenChangeCause::Touch);
    return;
  }

  // Anywhere else: only the Flights table reacts (page the rows).
  if (nav.current() != kScreenFlights) return;

  FeaturedSplit split = splitFeaturedAndRest(rows());
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

// --- Firmware update: drawn from NetState::ota ------------------------------

// The arduino-esp32 core marks a freshly updated image valid right at boot
// unless this says "later" — net_task does it once the board has proven it
// runs (ota_update::shouldMarkAppValid(), including "this UI loop is alive").
extern "C" bool verifyRollbackLater() { return true; }

static uint32_t otaSeenSeq = 0;
static bool otaScreenDrawn = false;
static uint32_t otaFailedAtMs = 0;  // != 0: "update failed" on screen since then
static constexpr uint32_t kOtaFailedShowMs = 5000;  // long enough to read on the board

// True while the OTA screens own the display (running, done, a failure on show).
static bool serviceOtaScreen() {
  const OtaProgress &o = net->ota;
  if (o.seq != otaSeenSeq) {
    otaSeenSeq = o.seq;
    if (o.phase == OtaProgress::Phase::Running && o.written == 0) otaScreenDrawn = false;
    if (!otaScreenDrawn) {
      backlightFullNow(tft);
      drawOtaScreen(tft, firmwareVersion(), o.fromGithub);
      otaScreenDrawn = true;
    }
    switch (o.phase) {
      case OtaProgress::Phase::Running:
        drawOtaProgress(tft, firmwareVersion(), o.newVersion.c_str(), o.written, o.total);
        break;
      case OtaProgress::Phase::Done:
        drawOtaResult(tft, true);  // net_task restarts the board in a moment
        break;
      case OtaProgress::Phase::Failed:
        drawOtaResult(tft, false);
        otaFailedAtMs = millis() | 1;
        break;
      default:
        break;
    }
  }
  if (o.phase == OtaProgress::Phase::Running || o.phase == OtaProgress::Phase::Done ||
      net->restartPending) {
    alertScreen.open = false;  // the OTA screen comes first
    return true;
  }
  if (otaFailedAtMs != 0) {
    if (millis() - otaFailedAtMs < kOtaFailedShowMs) return true;
    otaFailedAtMs = 0;  // back to the normal screens
    otaScreenDrawn = false;
    pendingRedraw.firstDraw = true;
  }
  return false;
}

// --- New version on GitHub: the question on the display ------------------------

// "NEW VERSION AVAILABLE — INSTALL IT?" — shown at most once per boot, over
// the normal screens, until YES / NO / 30 s.
static bool promptActive = false;
static bool promptShownThisBoot = false;
static bool promptReleased = false;  // finger lifted since the prompt appeared
static bool promptWasPressed = false;
static uint32_t promptShownAtMs = 0;
static uint32_t promptTimerDrawnMs = 0;
static std::string promptTag;
static constexpr uint32_t kPromptTimerStepMs = 250;

static void maybeShowUpdatePrompt(bool touchPressed) {
  const UpdateSummary &u = net->update;
  PromptGate g;
  g.status = u.status;
  g.latestTag = u.latestTag;
  g.skippedTag = cfg().update_skipped_tag;
  g.installedIsDev = isDevelopmentBuild(parseFwVersion(u.installed));
  g.alreadyPromptedThisBoot = promptShownThisBoot;
  g.homeConfigured = cfg().home_configured;
  g.currentScreen = nav.current();
  const CpaPrediction cpa = currentCpa();
  g.cpaFound = cpa.found;
  g.tCpaSeconds = static_cast<int>(cpa.t_cpa_seconds);
  g.touchActive = touchPressed;
  g.screenOff = backlightDecision().screen_off;
  g.alertScreenOpen = alertScreen.open;
  if (!shouldShowUpdatePrompt(g)) return;

  promptActive = true;
  promptShownThisBoot = true;
  promptReleased = false;
  promptWasPressed = true;
  promptShownAtMs = millis();
  promptTimerDrawnMs = promptShownAtMs;
  promptTag = u.latestTag;
  Serial.printf("[update] asking on the display: %s -> %s\n", u.installed.c_str(),
                promptTag.c_str());
  drawUpdatePromptScreen(tft, u.installed, promptTag);
}

// YES / NO go to net_task as a NetCommand. If the queue is full the command
// waits here and is retried on every loop() pass until it fits — the answer
// must not get lost (spec 09 §2).
static PendingCommand pendingCmd;

static void submitUpdateCommand(const NetCommand &c) {
  pendingCmd = submitCommand(c, core_link::sendCommand);
  if (pendingCmd.logQueueFull) Serial.println("[update] command queue full, retrying");
}

// While the prompt is up it owns the touchscreen; returns once answered.
static void serviceUpdatePrompt() {
  const uint32_t shownFor = millis() - promptShownAtMs;
  const TouchPoint t = touchInputRead(tft);
  if (!t.pressed) promptReleased = true;
  if (t.pressed && !promptWasPressed && acceptPromptTap(promptReleased, shownFor)) {
    const ResetChoice c = confirmHit(t.x, t.y, static_cast<int16_t>(tft.width()),
                                     static_cast<int16_t>(tft.height()));
    if (c == ResetChoice::Yes) {
      promptActive = false;
      // net_task starts the install; its OTA screen follows. If it can't
      // (no longer on offer), the normal screens come back.
      submitUpdateCommand(makeNetCommand(NetCommandType::InstallTag, promptTag));
      pendingRedraw.firstDraw = true;
      return;
    }
    if (c == ResetChoice::No) {
      submitUpdateCommand(makeNetCommand(NetCommandType::SkipTag, promptTag));
      promptActive = false;
      pendingRedraw.firstDraw = true;
      return;
    }
  }
  promptWasPressed = t.pressed;
  if (shownFor >= kUpdatePromptTimeoutMs) {
    promptActive = false;  // "later": asked again after the next boot
    pendingRedraw.firstDraw = true;
    return;
  }
  if (millis() - promptTimerDrawnMs >= kPromptTimerStepMs) {
    promptTimerDrawnMs = millis();
    drawUpdatePromptTimer(tft, kUpdatePromptTimeoutMs - shownFor);
  }
}

// --- Forgotten web panel password: hold a finger on the screen at power-up
// (runs in setup(), before net_task exists — the delay() loops are fine here).

static void offerWebPasswordReset(Config &bootCfg) {
  if (bootCfg.web_password.empty() || !touchInputRead(tft).pressed) return;

  const int16_t w = static_cast<int16_t>(tft.width());
  const int16_t h = static_cast<int16_t>(tft.height());
  drawResetHoldScreen(tft);
  const uint32_t holdStart = millis();
  uint32_t lastPressedMs = holdStart;
  for (;;) {
    if (touchInputRead(tft).pressed) lastPressedMs = millis();
    const uint32_t held = millis() - holdStart;
    const ResetHoldState st = resetHoldState(millis() - lastPressedMs, held);
    if (st == ResetHoldState::Released) {
      tft.fillScreen(LCARS_BLACK);
      return;
    }
    drawResetHoldProgress(tft, held);
    if (st == ResetHoldState::Done) break;
    delay(30);
  }

  // Wait for the finger to come off, then ask.
  uint32_t releasedSince = millis();
  while (millis() - releasedSince < kResetReleaseGraceMs) {
    if (touchInputRead(tft).pressed) releasedSince = millis();
    delay(20);
  }
  drawResetConfirmScreen(tft);
  const uint32_t askedAt = millis();
  bool wasPressed = false;
  while (millis() - askedAt < kResetConfirmTimeoutMs) {
    const TouchPoint t = touchInputRead(tft);
    if (t.pressed && !wasPressed) {
      const ResetChoice c = confirmHit(t.x, t.y, w, h);
      if (c == ResetChoice::Yes) {
        bootCfg.web_password.clear();
        saveConfig(bootCfg);
        bootCfg = loadConfig();
        Serial.println("[auth] web password cleared from the touchscreen");
        drawResetDoneScreen(tft);
        delay(1500);
        break;
      }
      if (c == ResetChoice::No) break;
    }
    wasPressed = t.pressed;
    delay(20);
  }
  tft.fillScreen(LCARS_BLACK);
}

// --- /screenshot.bmp: the web handler (core 0) asks, this loop reads -------
//
// One request at a time, handed over by ScreenshotBridge (screenshot.h): the
// handler waits by polling (1 ms steps); every request carries a generation,
// and a timed-out or ended one is invalidated under the lock, so a band this
// loop is still reading never writes into a buffer the handler has freed nor
// leaves "done" behind (spec 09 §1).

namespace shot {

ScreenshotBridge<SpinLock> bridge(ScreenshotBridge<SpinLock>::Hooks{
    [] { return static_cast<uint32_t>(millis()); }, [] { core_link::wakeUi(); },
    [] { vTaskDelay(1); }});

// ---- called on core 0 (config_portal's handler) — ScreenshotSource wants
// plain function pointers.

bool begin(int screenIndex, uint32_t timeoutMs) { return bridge.begin(screenIndex, timeoutMs); }

void requestBand(int bandY0, int bandRows, uint32_t rowStride, uint8_t *dst) {
  bridge.requestBand(bandY0, bandRows, rowStride, dst);
}

bool waitBand(uint32_t timeoutMs) { return bridge.waitBand(timeoutMs); }

void end() { bridge.end(); }

}  // namespace shot

// ?screen=N: switch to that screen and repaint it right now, so the capture
// shows it. Two more indices after the real screens, drawn from demo data:
// the update question (release_check, docs/screenshots/update_prompt.png)
// and the alert message screen (device_alerts, docs/screenshots/alert_screen.png).
static constexpr int kScreenshotUpdatePrompt = kScreenCount;
static constexpr int kScreenshotAlertScreen = kScreenCount + 1;

static void screenshotPrepare(int screenIndex) {
  if (screenIndex == kScreenshotUpdatePrompt || screenIndex == kScreenshotAlertScreen) {
    if (screenIndex == kScreenshotUpdatePrompt) {
      const UpdateState demo = updateDemoState(/*withPrerelease=*/false);
      drawUpdatePromptScreen(tft, demo.installed, demo.latest.tag);
      drawUpdatePromptTimer(tft, kUpdatePromptTimeoutMs * 2 / 3);
    } else {
      const AlertDemo demo = alertDemoState();
      drawAlertScreen(tft, demo.alerts, demo.details, demo.details.quotaObservedMs, 12);
    }
    alertScreen.open = false;
    pendingRedraw = RedrawEvents{};
    pendingRedraw.firstDraw = true;  // repaints the real screen once the capture is out
    return;
  }
  if (alertScreen.open) closeAlertMessage();  // a normal screen was asked for
  if (screenIndex >= 0 && nav.set(screenIndex)) onScreenChanged(ScreenChangeCause::Screenshot);
  const uint8_t mask = computeRedrawMask(pendingRedraw, nav.current());
  if (mask != 0 && cfg().home_configured) applyRedraw(mask);
  pendingRedraw = RedrawEvents{};
}

// The screen to put back once a ?screen=N capture is over (spec 10).
static ScreenshotReturn shotReturn;

// UI side: answers whatever the handler asked for. True while a capture runs.
// When it is over (end(), timeout or the 5 s idle rule), the screen shown
// before the capture comes back — repainted, not saved.
static bool serviceScreenshot() {
  using Job = ScreenshotBridge<SpinLock>::Job;
  const Job job = shot::bridge.takeJob();
  if (job.kind == Job::Prepare) {
    shotReturn.onPrepare(nav.current(), job.screen, kScreenCount);
    screenshotPrepare(job.screen);
  } else if (job.kind == Job::Band) {
    static lgfx::rgb888_t line[320];
    static uint8_t bgr[320 * 3];
    const int w = static_cast<int>(tft.width());
    for (int i = 0; i < job.rows; ++i) {
      const int y = job.y0 + job.rows - 1 - i;  // bottom row of the band first (BMP)
      tft.readRect(0, y, w, 1, line);
      for (int x = 0; x < w; ++x) {
        bgr[3 * x + 0] = line[x].b;
        bgr[3 * x + 1] = line[x].g;
        bgr[3 * x + 2] = line[x].r;
      }
      if (!shot::bridge.writeRow(job, i, bgr, 3 * w)) break;  // timed out / ended: drop it
    }
  }
  shot::bridge.finishJob(job, millis());
  const bool capturing = shot::bridge.capturing(millis());
  const int back = shotReturn.afterPass(capturing);
  if (back >= 0 && nav.set(back)) onScreenChanged(ScreenChangeCause::Screenshot);
  return capturing;
}

// --- Perf: UI loop gaps (perf_stats), handed to net_task once a minute --------
//
// The "gap" is how much later than planned this pass started: the time since
// the previous pass minus the sleep that pass asked for — i.e. its own drawing
// time plus any oversleep. A responsive UI keeps it at a few ms; a full
// repaint shows up as ~100 ms.

static GapStats uiWindow;
static UiPerfShare uiShare;
static uint32_t uiWindowStartMs = 0;
static uint32_t lastLoopMs = 0;
static uint32_t plannedSleepMs = 0;  // what the previous pass asked to sleep

static void perfLoopTick() {
  const uint32_t now = millis();
  if (lastLoopMs != 0) {
    const uint32_t since = now - lastLoopMs;
    const uint32_t gap = since > plannedSleepMs ? since - plannedSleepMs : 0;
    uiWindow.add(gap);
    if (gap > uiShare.maxEverMs) uiShare.maxEverMs = gap;
    if (gap >= kPerfGapBucketMs[3]) ++uiShare.slowEver;
  }
  lastLoopMs = now;
  if (uiWindowStartMs == 0) uiWindowStartMs = now;
  if (now - uiWindowStartMs >= kPerfWindowMs) {
    uiShare.windowCount = uiWindow.count;
    uiShare.windowMaxMs = uiWindow.maxMs;
    for (int i = 0; i < kPerfGapBuckets; ++i) uiShare.buckets[i] = uiWindow.buckets[i];
    uiShare.stackFree = uxTaskGetStackHighWaterMark(nullptr);
    core_link::uiPerf.set(uiShare);
    uiWindow = GapStats{};
    uiWindowStartMs = now;
  }
}

// --- Sleep until the next thing to do --------------------------------------------

static uint32_t lastFlightTickMs = 0;

static void sleepUntilNextDeadline() {
  const uint32_t now = millis();
  UiWake w;
  if (nav.current() == kScreenFlight) {
    w.untilCountdownMs = msUntilPeriod(now, lastFlightTickMs, 1000);
  }
  if (isTimeSynced()) {
    const std::time_t local = timeSyncNowLocal();
    w.untilMinuteMs = msUntilNextMinute(static_cast<int>(local % 60), 0);
  }
  if (promptActive) {
    w.untilPromptTimerMs = msUntilPeriod(now, promptTimerDrawnMs, kPromptTimerStepMs);
  }
  if (alertScreen.open) w.untilAlertTimerMs = alertScreenMsToNextSecond(alertScreen, now);
  plannedSleepMs = uiSleepMs(w);
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(plannedSleepMs));
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

  offerWebPasswordReset(bootCfg);  // finger on the screen at power-up + a password set
  nav.set(bootCfg.last_screen);    // restore the last-active screen
  lastPersistedScreen = nav.current();
  lastScreenChangeMs = millis();
  lastScreenPersistMs = millis();

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
  shotSrc.begin = shot::begin;
  shotSrc.requestBand = shot::requestBand;
  shotSrc.waitBand = shot::waitBand;
  shotSrc.end = shot::end;
  configPortalSetScreenshotSource(shotSrc);

  core_link::begin(xTaskGetCurrentTaskHandle());
  core_link::uiBeatMs.store(millis());
  core_link::netBeatMs.store(millis());
  netTaskStart(cfgPtr, appPendingVerify);

  pendingRedraw.firstDraw = true;  // paint the (empty) active screen + chrome immediately
}

void loop() {
  perfLoopTick();
  core_link::uiBeatMs.store(millis());
  pendingCmd = retryPendingCommand(pendingCmd, core_link::sendCommand);

  takeNetState();
  updateShownHealth();

  // Backlight: sensor, night mode, ramps. Service screens get full brightness.
  const bool otaOnScreen = net->ota.phase == OtaProgress::Phase::Running ||
                           net->ota.phase == OtaProgress::Phase::Done || otaFailedAtMs != 0 ||
                           net->restartPending;
  backlightLoop(tft, cfg(), promptActive || otaOnScreen);
  core_link::pollingAllowed.store(
      shouldPollOpenSky(backlightDecision(), cfg().night_pause_polling));

  // --- Firmware update in progress (or just failed): its screen only ---------
  if (serviceOtaScreen()) {
    pendingRedraw = RedrawEvents{};
    sleepUntilNextDeadline();
    return;
  }

  // --- A screenshot is being taken: hold still ---------------------------------
  if (serviceScreenshot()) {
    sleepUntilNextDeadline();
    return;
  }

  // --- The update question owns the screen until it's answered ----------
  if (promptActive && net->update.installPending) {
    promptActive = false;  // installed from the config page meanwhile: the OTA screen follows
  }
  if (promptActive) {
    serviceUpdatePrompt();
    if (promptActive) {
      sleepUntilNextDeadline();
      return;
    }
  }

  // --- The alert message screen owns the screen while it's open --------------
  static bool wasTouchPressed = false;
  if (alertScreen.open) {
    serviceAlertScreen();
    if (alertScreen.open) {
      wasTouchPressed = true;  // whatever the finger does here isn't a tap below
      pendingRedraw = RedrawEvents{};
      sleepUntilNextDeadline();
      return;
    }
    wasTouchPressed = touchInputRead(tft).pressed;  // the CLOSE tap isn't one either
  }

  // --- Touch: edge-detected, one handleTap() per press --------------------
  TouchPoint touch = touchInputRead(tft);
  if (touch.pressed && !wasTouchPressed) {
    // One line per tap: raw XPT2046 values + where they mapped to — what
    // touch calibration is tuned from (include/touch_calibration.h).
    Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d) nav=%d\n", touch.raw_x, touch.raw_y,
                  touch.x, touch.y,
                  static_cast<int>(navHitTest(touch.x, touch.y, static_cast<int16_t>(tft.width()),
                                              static_cast<int16_t>(tft.height()))
                                       .action));
    if (backlightHandleTouch(cfg())) {
      lastScreenChangeMs = millis();  // only woke a dark screen: no tap, a fresh auto-cycle
    } else {
      handleTap(touch.x, touch.y);
    }
  }
  wasTouchPressed = touch.pressed;

  if (alertScreen.open) {  // the tap opened the "!" message
    pendingRedraw = RedrawEvents{};
    sleepUntilNextDeadline();
    return;
  }

  // --- New version on GitHub: ask once ------------------------------------
  if (!promptActive && !net->update.installPending) maybeShowUpdatePrompt(touch.pressed);
  if (promptActive) {
    pendingRedraw = RedrawEvents{};
    sleepUntilNextDeadline();
    return;
  }

  // --- Flight screen: tick the countdown ~1 Hz between polls --------------
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

  // --- Auto screen cycling (docs/screens.md "Auto screen cycling") ------------
  if (cfg().auto_cycle_enabled && cfg().home_configured &&
      !backlightDecision().screen_off) {  // nobody sees a dark screen change
    CpaPrediction cpa = currentCpa();
    bool deferHold = shouldDeferAutoSwitch(nav.current(), cpa.found, cpa.t_cpa_seconds);
    if (shouldAutoAdvance(millis() - lastScreenChangeMs, cfg().auto_cycle_interval_s,
                          deferHold)) {
      nav.next();                                // 3 distinct screens, wraps
      onScreenChanged(ScreenChangeCause::AutoCycle);  // resets the timer + page, forces redraw
    }
  }

  // --- Redraw only what changed (CLAUDE.md "Redraw policy") -------------
  const uint8_t mask = computeRedrawMask(pendingRedraw, nav.current());
  if (mask != 0) {
    if (cfg().home_configured) {
      applyRedraw(mask);
    } else if (mask & kRedrawFull) {
      showSetupPrompt();  // "set your home location" until config_portal is used
    }
  }
  pendingRedraw = RedrawEvents{};

  sleepUntilNextDeadline();
}
