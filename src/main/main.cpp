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

// The UI's own bookkeeping (ui_flow: redraw events, shown health, page,
// screen changes, ticks, touch edges), the service screens (OTA, update
// question, alert message) and the loop's timing for /perf.json.
static UiFlow ui;
static ServiceScreens services;
static UiPerfMeter perfMeter;

// The arduino-esp32 core marks a freshly updated image valid right at boot
// unless this says "later" — net_task does it once the board has proven it
// runs (ota_update::shouldMarkAppValid(), including "this UI loop is alive").
extern "C" bool verifyRollbackLater() { return true; }

static const Settings &cfg() { return *net->settings; }

static UiView view() {
  return makeUiView(*net, nav.current(), ui.pager.page(), ui.shownHealth, millis(),
                    timeSyncNowLocal(), isTimeSynced());
}

// The regions the pending events ask for. Without a home location the setup
// prompt stands in for the screens (`setupPrompt`: on a full redraw).
static void redrawPending(bool setupPrompt) {
  const uint8_t mask = ui.takeRedrawMask(nav.current());
  if (mask != 0 && cfg().home.configured) {
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
    if (e.languageChanged) i18nSetLanguage(next->settings->display.language);
    net = std::move(next);
    ui.takeNetEvents(e, *net);
  }
  if (ui.updateHealth(*net, millis(), core_link::netBeatMs.load())) {
    Serial.println("[main] network task silent for 60 s");
  }
}

// After nav changed the active screen; last_screen is one NVS key.
static void onScreenChanged(ScreenChangeCause cause) {
  if (ui.screenChanged(nav.current(), cause, millis())) saveLastScreen(nav.current());
}

// One tap (edge-detected, once per press): routeTap() says what it is.
static void onTap(int16_t x, int16_t y) {
  const TapContext c = tapContextOf(view(), static_cast<int16_t>(tft.width()),
                                    static_cast<int16_t>(tft.height()), i18nLanguage());
  const TapAction a = routeTap(x, y, c);
  if (a.kind == TapKind::OpenAlerts) {
    services.openAlerts(tft, *net);
  } else if (a.kind == TapKind::JumpTo) {
    if (nav.set(a.target)) onScreenChanged(ScreenChangeCause::Touch);
  } else {
    ui.pageTap(a.kind, c.pageCount);
  }
}

// ?screen=N of /screenshot.bmp: switch to that screen and repaint it now, or
// draw one of the two demo screens over it (repainted once the capture is out).
static void prepareCapture(int screenIndex) {
  const ScreenshotPrepare p = screenshotPrepareTarget(screenIndex, kScreenCount);
  if (p.kind == ScreenshotPrepareKind::UpdatePromptDemo ||
      p.kind == ScreenshotPrepareKind::AlertDemo) {
    drawScreenshotDemo(tft, p.kind);
    ui.apply(services.screenshotDemoShown(), millis());
    return;
  }
  ui.apply(services.closeAlertScreen(), millis());  // a normal screen was asked for
  if (p.kind == ScreenshotPrepareKind::Screen && nav.set(p.screen)) {
    onScreenChanged(ScreenChangeCause::Screenshot);
  }
  redrawPending(/*setupPrompt=*/false);
}

// The service screens, first come first: the OTA screen, a screenshot (hold
// still; afterwards the screen shown before it comes back, repainted, not
// saved), the update question, the alert message. True = one owns the display.
static bool serviceScreensPass() {
  if (ui.apply(services.otaPass(tft, *net, firmwareVersion()), millis())) return true;
  const ScreenshotPassResult shot =
      screenshotPanelPass(tft, nav.current(), kScreenCount, prepareCapture);
  if (shot.backTo >= 0 && nav.set(shot.backTo)) onScreenChanged(ScreenChangeCause::Screenshot);
  if (shot.capturing) return true;
  return ui.apply(services.dialogsPass(tft, *net), millis());
}

// The normal screens: a tap, the update question (asked once), the NOW
// countdown and header clock ticks, auto-cycle, then the redraw.
static void screensPass() {
  const TouchPoint touch = touchInputRead(tft);
  if (ui.touch.tap(touch.pressed)) {
    touchLogTap(tft, touch);
    if (backlightHandleTouch(cfg().backlight)) {
      ui.screenChanges.restartAutoCycle(millis());  // it only woke a dark screen
    } else {
      onTap(touch.x, touch.y);
    }
  }
  const bool screenOff = backlightDecision().screen_off;
  services.askAboutUpdateIfDue(tft, *net, nav.current(), touch.pressed, screenOff);
  if (ui.apply(services.afterNormalTap(), millis())) return;

  // -1 while unsynced, so the first NTP sync also counts as a minute change.
  const long minuteNow = isTimeSynced() ? minuteOf(timeSyncNowLocal()) : -1;
  if (ui.tick(*net, nav.current(), millis(), minuteNow, screenOff)) {
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
  ui.addWakeTimes(w, nav.current(), now);
  if (isTimeSynced()) {
    w.untilMinuteMs = msUntilNextMinute(static_cast<int>(timeSyncNowLocal() % 60), 0);
  }
  services.addWakeTimes(w, now);
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

  Settings bootCfg = loadSettings();
  i18nSetLanguage(bootCfg.display.language);  // before anything is drawn
  Serial.printf("[ota] %s\n", firmwareMarker());
  const bool appPendingVerify = otaFlashRunningPendingVerify();

  // The NOW screen's countdown sprite (~30 KB), before WiFi and net_task start
  // allocating — created later it split the largest free block (spec/04 §10).
  flightScreenReserveSprite(static_cast<int16_t>(tft.width()));

  // A forgotten panel password: a finger on the screen at power-up (before
  // net_task exists — the blocking delay() steps are fine here).
  runPasswordReset(tft, !bootCfg.web.password.empty(), [&bootCfg] {
    bootCfg.web.password.clear();
    saveSettings(bootCfg);
    bootCfg = loadSettings();
    Serial.println("[auth] web password cleared from the touchscreen");
  });
  nav.set(bootCfg.display.last_screen);  // restore the last-active screen
  ui.screenChanges.begin(nav.current(), millis());

  // The first snapshot: the settings, no data yet — the chrome and "--" go on
  // screen right away, before WiFi is even up.
  auto cfgPtr = std::make_shared<const Settings>(bootCfg);
  auto first = std::make_shared<NetState>();
  first->settings = cfgPtr;
  net = first;

  ScreenshotSource shotSrc;
  shotSrc.width = static_cast<int>(tft.width());
  shotSrc.height = static_cast<int>(tft.height());
  shotSrc.screenCount = kScreenCount + 2;  // + the update prompt and alert screen demos
  shotSrc.begin = screenshotPanelBegin;
  shotSrc.requestBand = screenshotPanelRequestBand;
  shotSrc.waitBand = screenshotPanelWaitBand;
  shotSrc.end = screenshotPanelEnd;
  webServerSetScreenshotSource(shotSrc);

  core_link::begin(xTaskGetCurrentTaskHandle());
  core_link::uiBeatMs.store(millis());
  core_link::netBeatMs.store(millis());
  netTaskStart(cfgPtr, appPendingVerify);

  ui.pending.firstDraw = true;  // paint the (empty) active screen + chrome immediately
}

void loop() {
  if (perfMeter.tick(millis())) {  // a 60 s window closed: hand it to net_task
    UiPerfShare share = perfMeter.share();
    share.stackFree = uxTaskGetStackHighWaterMark(nullptr);
    core_link::uiPerf.set(share);
  }
  core_link::uiBeatMs.store(millis());
  services.retryCommand();
  takeNetState();

  const bool fullBright = services.holdsBacklight(net->ota, net->restartPending);  // service screens
  backlightLoop(tft, cfg().backlight, cfg().home, fullBright);  // sensor, night mode, ramps
  core_link::pollingAllowed.store(
      shouldPollOpenSky(backlightDecision(), cfg().backlight.night_pause_polling));

  if (!serviceScreensPass()) screensPass();
  sleepUntilNextDeadline();
}
