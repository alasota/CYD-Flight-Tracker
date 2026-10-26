#include "core_link.h"

#include <algorithm>

RedrawEvents diffNetState(const NetState *prev, const NetState &next) {
  RedrawEvents e;
  if (prev == nullptr) {
    e.firstDraw = true;
    return e;
  }

  if (prev->data != next.data) e.newPoll = true;

  if (prev->health != next.health) {
    e.healthChanged = true;
    // An empty screen says why it is empty (emptyStateText()), so it has to
    // be repainted when the reason changes, not only the header dot.
    if (next.data == nullptr || next.data->rows.empty()) e.newPoll = true;
  }

  if (prev->alerts.displaySeverity() != next.alerts.displaySeverity()) e.alertsChanged = true;

  if (prev->cfg != next.cfg && prev->cfg != nullptr && next.cfg != nullptr) {
    const Config &a = *prev->cfg;
    const Config &b = *next.cfg;
    if (a.language != b.language) e.languageChanged = true;
    if (a.home_configured != b.home_configured) e.firstDraw = true;
    // Radar rings are labelled from the radius; distances from home.
    if (a.radius_deg != b.radius_deg || a.home_lat != b.home_lat || a.home_lon != b.home_lon) {
      e.newPoll = true;
    }
  }
  return e;
}

namespace {
// Everything the texts show — not quotaObservedMs (see netStateDiffers()).
bool alertDetailsDiffer(const AlertDetails &a, const AlertDetails &b) {
  return a.loginHttpCode != b.loginHttpCode || a.quotaResetInS != b.quotaResetInS ||
         a.estimatedDailyCredits != b.estimatedDailyCredits || a.dailyLimit != b.dailyLimit ||
         a.pollIntervalS != b.pollIntervalS || a.minPollIntervalS != b.minPollIntervalS;
}
}  // namespace

bool netStateDiffers(const NetState &a, const NetState &b) {
  return a.data != b.data || a.cfg != b.cfg || a.cfgSeq != b.cfgSeq || a.health != b.health ||
         a.wifi != b.wifi || a.restartPending != b.restartPending ||
         a.update.status != b.update.status || a.update.installed != b.update.installed ||
         a.update.latestTag != b.update.latestTag ||
         a.update.installPending != b.update.installPending || a.ota.seq != b.ota.seq ||
         a.ota.phase != b.ota.phase || a.alerts != b.alerts ||
         alertDetailsDiffer(a.alertDetails, b.alertDetails);
}

NetCommand makeNetCommand(NetCommandType type, const std::string &tag) {
  NetCommand c;
  c.type = type;
  const size_t n = std::min(tag.size(), sizeof(c.tag) - 1);
  std::memcpy(c.tag, tag.data(), n);
  c.tag[n] = '\0';
  return c;
}

std::string netCommandTag(const NetCommand &c) {
  return std::string(c.tag, strnlen(c.tag, sizeof(c.tag)));
}

PendingCommand submitCommand(const NetCommand &c, const CommandSender &trySend) {
  PendingCommand p;  // replaces whatever was waiting (one slot, the newer wins)
  if (trySend(c)) return p;
  p.waiting = true;
  p.cmd = c;
  p.logQueueFull = true;
  return p;
}

PendingCommand retryPendingCommand(PendingCommand p, const CommandSender &trySend) {
  p.logQueueFull = false;
  if (p.waiting && trySend(p.cmd)) p.waiting = false;
  return p;
}

uint32_t msUntilPeriod(uint32_t nowMs, uint32_t lastMs, uint32_t periodMs) {
  const uint32_t elapsed = nowMs - lastMs;  // unsigned: rollover-safe
  return elapsed >= periodMs ? 0 : periodMs - elapsed;
}

uint32_t msUntilNextMinute(int secondOfMinute, uint32_t msIntoSecond) {
  if (secondOfMinute < 0) secondOfMinute = 0;
  if (secondOfMinute > 59) secondOfMinute = 59;
  if (msIntoSecond > 999) msIntoSecond = 999;
  return static_cast<uint32_t>(60 - secondOfMinute) * 1000 - msIntoSecond;
}

uint32_t uiSleepMs(const UiWake &w) {
  uint32_t ms = kUiMaxSleepMs;
  ms = std::min(ms, w.touchPollMs);
  ms = std::min(ms, w.untilCountdownMs);
  ms = std::min(ms, w.untilMinuteMs);
  ms = std::min(ms, w.untilPromptTimerMs);
  ms = std::min(ms, w.untilBacklightMs);
  ms = std::min(ms, w.untilAlertTimerMs);
  return ms;
}

bool heartbeatStale(uint32_t nowMs, uint32_t beatMs, uint32_t limitMs) {
  return nowMs - beatMs > limitMs;
}

#ifdef ARDUINO

#include <freertos/queue.h>

namespace core_link {

LatestBox<NetState> netState;
Guarded<UiPerfShare> uiPerf;
std::atomic<uint32_t> uiBeatMs{0};
std::atomic<uint32_t> netBeatMs{0};
std::atomic<bool> pollingAllowed{true};

namespace {
TaskHandle_t uiHandle = nullptr;
QueueHandle_t commands = nullptr;
constexpr UBaseType_t kCommandQueueLength = 8;
}  // namespace

void begin(TaskHandle_t uiTask) {
  uiHandle = uiTask;
  if (commands == nullptr) commands = xQueueCreate(kCommandQueueLength, sizeof(NetCommand));
}

void wakeUi() {
  if (uiHandle != nullptr) xTaskNotifyGive(uiHandle);
}

bool sendCommand(const NetCommand &c) {
  return commands != nullptr && xQueueSend(commands, &c, 0) == pdTRUE;
}

bool receiveCommand(NetCommand *out) {
  return commands != nullptr && xQueueReceive(commands, out, 0) == pdTRUE;
}

}  // namespace core_link

#endif  // ARDUINO
