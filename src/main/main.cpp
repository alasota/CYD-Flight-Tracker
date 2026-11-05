// CYD Sky Tracker — the UI on core 1: setup() and loop(). Wiring only: what
// happens is decided in display/services (ui_flow, the service-screen flows,
// ui_perf) and drawn by display/adapters (ui_draw, service_flows,
// screenshot_panel). net_task on core 0 does all the networking and publishes
// NetState snapshots (core_link); it never draws, and this loop never waits
// for the network. One pass: the newest snapshot, the backlight, then either a
// service screen (OTA, screenshot, update question, alert message) or touch,
// ticks and auto-cycle; redraw only what changed (CLAUDE.md "Redraw policy");
// sleep until the next deadline. docs/architecture.md "Concurrency — two cores".

#include <Arduino.h>

#include <memory>

#include "admin/adapters/web_server.h"
#include "admin/core/config_store.h"
#include "admin/core/firmware/ota_update.h"
#include "admin/core/firmware/version.h"
#include "display/adapters/backlight_ldr.h"
#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/adapters/lgfx/flight_screen_draw.h"  // flightScreenReserveSprite()
#include "display/adapters/lgfx/screenshot_panel.h"
#include "display/adapters/lgfx/ui_draw.h"
#include "display/adapters/service_flows.h"
#include "display/adapters/touch_xpt2046.h"
#include "display/core/backlight.h"
#include "display/core/i18n.h"
#include "display/core/redraw_policy.h"
#include "display/core/screen_nav.h"
#include "display/services/ui_flow.h"
#include "display/services/ui_perf.h"
#include "display/services/update_prompt_flow.h"
#include "main/net_task.h"
#include "platform/core_link.h"
#include "platform/time_sync.h"
#include "shared/clock.h"  // minuteOf()

// No SET_LOOP_TASK_STACK_SIZE: the TLS handshakes that needed 16 KB run in
// net_task; drawing fits the core's default 8 KB (checked in [perf]).

static LGFX tft;
static ScreenNav nav;  // the active screen, restored from last_screen at boot

// The latest snapshot from net_task, and its sequence number in the box.
static std::shared_ptr<const NetState> net;
static uint32_t netSeen = 0;

// The UI's own bookkeeping (ui_flow) and the service screens' flows.
static ScreenChangeTracker screenChanges;
static TablePager pager;
static MinuteWatch minuteWatch;
static CountdownTick countdownTick;
static TouchEdge touchEdge;
static OtaScreenFlow otaScreen;
static UpdatePromptFlow prompt;
static AlertScreenFlow alertScreen;
static UiPerfMeter perfMeter;

// What happened since the last redraw (computeRedrawMask()), the health the
// header shows, and a YES / NO answer still waiting for room in the command
// queue — retried every pass, it must not get lost (spec 09 §2).
static RedrawEvents pendingRedraw;
static OpenSkyHealth shownHealth = OpenSkyHealth::Ok;
static PendingCommand pendingCmd;

// The arduino-esp32 core marks a freshly updated image valid right at boot
// unless this says "later" — net_task does it once the board has proven it
// runs (ota_update::shouldMarkAppValid(), including "this UI loop is alive").
extern "C" bool verifyRollbackLater() { return true; }

static const Config &cfg() { return *net->cfg; }

static UiView view() {
  return makeUiView(*net, nav.current(), pager.page(), shownHealth, millis(), timeSyncNowLocal(),
                    isTimeSynced());
}

// The regions pendingRedraw asks for, then nothing pending. Without a home
// location the setup prompt stands in for the screens (`setupPrompt`: on a full redraw).
static void redrawPending(bool setupPrompt) {
  const uint8_t mask = computeRedrawMask(pendingRedraw, nav.current());
  pendingRedraw = RedrawEvents{};
  if (mask != 0 && cfg().home_configured) {
    applyRedraw(tft, mask, view());
  } else if (setupPrompt && (mask & kRedrawFull)) {
    drawSetupPrompt(tft);  // "set your home location" until web_server is used
  }
}

// A new snapshot from net_task (if there is one) -> redraw events; then the
// header's health (NetworkError once net_task has been silent for 60 s).
static void takeNetState() {
  std::shared_ptr<const NetState> next;
  if (core_link::netState.takeIfNewer(netSeen, next)) {
    const RedrawEvents e = diffNetState(net.get(), *next);
    if (next->cfg->language != net->cfg->language) i18nSetLanguage(next->cfg->language);
    net = std::move(next);
    pendingRedraw.firstDraw |= e.firstDraw;
    pendingRedraw.languageChanged |= e.languageChanged;
    pendingRedraw.newPoll |= e.newPoll;
    pendingRedraw.minuteChanged |= e.minuteChanged;
    pendingRedraw.alertsChanged |= e.alertsChanged;
    if (e.newPoll) pager.clamp(tablePageCount(netRows(*net)));  // a shorter list: its last page
  }
  const ShownHealthStep h = shownHealthStep(shownHealth, net->health, millis(),
                                            core_link::netBeatMs.load(), netRows(*net).empty());
  if (!h.healthChanged) return;
  if (h.stalled) Serial.println("[main] network task silent for 60 s");
  shownHealth = h.shown;
  pendingRedraw.healthChanged = true;
  pendingRedraw.newPoll |= h.newPoll;
}

// After nav changed the active screen: page 0, a fresh auto-cycle interval, a
// redraw, and last_screen written when screenChanges says so (a single NVS key).
static void onScreenChanged(ScreenChangeCause cause) {
  if (screenChanges.onChange(nav.current(), cause, millis(), pager, pendingRedraw).persist) {
    saveLastScreen(nav.current());
  }
}

// Back from the alert message: same screen and page, a full repaint, a fresh auto-cycle.
static void closeAlertMessage() {
  alertScreen.closeQuietly();
  screenChanges.restartAutoCycle(millis());
  pendingRedraw.firstDraw = true;
}

// One tap (edge-detected, once per press): routeTap() says what it is.
static void onTap(int16_t x, int16_t y) {
  const TapContext c = tapContextOf(view(), static_cast<int16_t>(tft.width()),
                                    static_cast<int16_t>(tft.height()), i18nLanguage());
  const TapAction a = routeTap(x, y, c);
  if (a.kind == TapKind::OpenAlerts) {
    showAlertScreen(tft, alertScreen, net->alerts, net->alertDetails);
  } else if (a.kind == TapKind::JumpTo) {
    if (nav.set(a.target)) onScreenChanged(ScreenChangeCause::Touch);
  } else if (a.kind == TapKind::PagePrev || a.kind == TapKind::PageNext) {
    if (a.kind == TapKind::PagePrev ? pager.prev() : pager.next(c.pageCount)) {
      pendingRedraw.pageChanged = true;
    }
  }
}

// ?screen=N of /screenshot.bmp: switch to that screen and repaint it now, or
// draw one of the two demo screens over it (repainted once the capture is out).
static void prepareCapture(int screenIndex) {
  const ScreenshotPrepare p = screenshotPrepareTarget(screenIndex, kScreenCount);
  if (p.kind == ScreenshotPrepareKind::UpdatePromptDemo ||
      p.kind == ScreenshotPrepareKind::AlertDemo) {
    drawScreenshotDemo(tft, p.kind);
    alertScreen.closeQuietly();
    pendingRedraw = RedrawEvents{};
    pendingRedraw.firstDraw = true;
    return;
  }
  if (alertScreen.isOpen()) closeAlertMessage();  // a normal screen was asked for
  if (p.kind == ScreenshotPrepareKind::Screen && nav.set(p.screen)) {
    onScreenChanged(ScreenChangeCause::Screenshot);
  }
  redrawPending(/*setupPrompt=*/false);
}

// The service screens, first come first. True = one of them owns the display
// this pass and the normal screens wait.
static bool serviceScreensPass() {
  const OtaScreenStep ota =
      runOtaScreen(tft, otaScreen, net->ota, net->restartPending, firmwareVersion());
  if (ota.backToScreens) pendingRedraw.firstDraw = true;
  if (ota.ownsDisplay) {
    alertScreen.closeQuietly();  // the OTA screen comes first
    pendingRedraw = RedrawEvents{};
    return true;
  }
  // A screenshot is being taken: hold still. Afterwards the screen shown
  // before the capture comes back — repainted, not saved.
  const ScreenshotPassResult shot =
      screenshotPanelPass(tft, nav.current(), kScreenCount, prepareCapture);
  if (shot.backTo >= 0 && nav.set(shot.backTo)) onScreenChanged(ScreenChangeCause::Screenshot);
  if (shot.capturing) return true;

  // The update question, until it's answered (an install from the settings
  // page closes it: the OTA screen follows).
  if (prompt.active() && net->update.installPending) prompt.supersededByInstall();
  if (prompt.active()) {
    const PromptStep r = runUpdatePrompt(tft, prompt);
    if (r.command.type != NetCommandType::None) {
      pendingCmd = submitCommand(r.command, core_link::sendCommand);
      if (pendingCmd.logQueueFull) Serial.println("[update] command queue full, retrying");
    }
    if (r.firstDraw) pendingRedraw.firstDraw = true;
    if (prompt.active()) return true;
  }

  // The alert message: its finger (the CLOSE tap too) is no tap below.
  if (!alertScreen.isOpen()) return false;
  const AlertFlowStep r = runAlertScreen(tft, alertScreen, net->alerts, net->alertDetails);
  if (r.swallowTouch) touchEdge.swallow();
  if (r.close) closeAlertMessage();
  if (!alertScreen.isOpen()) return false;
  pendingRedraw = RedrawEvents{};
  return true;
}

// The normal screens: a tap, the update question (asked once), the NOW
// countdown and header clock ticks, auto-cycle, then the redraw.
static void screensPass() {
  const TouchPoint touch = touchInputRead(tft);
  if (touchEdge.tap(touch.pressed)) {
    touchLogTap(tft, touch);
    if (backlightHandleTouch(cfg())) {
      screenChanges.restartAutoCycle(millis());  // it only woke a dark screen
    } else {
      onTap(touch.x, touch.y);
    }
  }
  if (!alertScreen.isOpen() && !prompt.active() && !net->update.installPending &&
      prompt.tryOpen(updatePromptGate(*net, nav.current(), millis(), touch.pressed,
                                      backlightDecision().screen_off, false),
                     net->update.latestTag, millis())) {
    openUpdatePrompt(tft, prompt, net->update.installed);
  }
  if (alertScreen.isOpen() || prompt.active()) {  // the tap opened the "!", or the question is up
    pendingRedraw = RedrawEvents{};
    return;
  }

  if (countdownTick.due(nav.current(), millis())) pendingRedraw.countdownTick = true;
  // -1 while unsynced, so the first NTP sync also counts as a change.
  if (minuteWatch.changed(isTimeSynced() ? minuteOf(timeSyncNowLocal()) : -1)) {
    pendingRedraw.minuteChanged = true;
  }
  if (shouldAutoCycleNow(autoCycleInputAt(*net, nav.current(), backlightDecision().screen_off,
                                          screenChanges.autoCycleElapsedMs(millis()), millis()))) {
    nav.next();  // 3 distinct screens, wraps
    onScreenChanged(ScreenChangeCause::AutoCycle);
  }
  redrawPending(/*setupPrompt=*/true);
}

// Sleeps until the nearest deadline (touch poll, the countdown second, the
// clock minute, the service screens' timers) or until net_task publishes.
static void sleepUntilNextDeadline() {
  const uint32_t now = millis();
  UiWake w;
  if (nav.current() == kScreenFlight) w.untilCountdownMs = countdownTick.msUntilNext(now);
  if (isTimeSynced()) {
    w.untilMinuteMs = msUntilNextMinute(static_cast<int>(timeSyncNowLocal() % 60), 0);
  }
  w.untilPromptTimerMs = prompt.msUntilTimer(now);
  if (alertScreen.isOpen()) w.untilAlertTimerMs = alertScreen.msToNextSecond(now);
  const uint32_t sleepMs = uiSleepMs(w);
  perfMeter.setPlannedSleep(sleepMs);
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(sleepMs));
}

void setup() {
  Serial.begin(115200);
  tft.init();           // also brings up the XPT2046 touch controller (LGFX_CYD.hpp)
  backlightBegin(tft);  // full brightness until the light sensor has settled
  tft.setRotation(1);   // landscape, 320x240
  tft.fillScreen(LCARS_BLACK);

  Config bootCfg = loadConfig();
  i18nSetLanguage(bootCfg.language);  // before anything is drawn
  Serial.printf("[ota] %s\n", firmwareMarker());
  const bool appPendingVerify = otaFlashRunningPendingVerify();

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
  nav.set(bootCfg.last_screen);  // restore the last-active screen
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
  if (perfMeter.tick(millis())) {  // a 60 s window closed: hand it to net_task
    UiPerfShare share = perfMeter.share();
    share.stackFree = uxTaskGetStackHighWaterMark(nullptr);
    core_link::uiPerf.set(share);
  }
  core_link::uiBeatMs.store(millis());
  pendingCmd = retryPendingCommand(pendingCmd, core_link::sendCommand);
  takeNetState();

  // Backlight: sensor, night mode, ramps. Service screens get full brightness.
  backlightLoop(tft, cfg(),
                prompt.active() || otaScreen.holdsBacklight(net->ota, net->restartPending));
  core_link::pollingAllowed.store(
      shouldPollOpenSky(backlightDecision(), cfg().night_pause_polling));

  if (!serviceScreensPass()) screensPass();
  sleepUntilNextDeadline();
}
