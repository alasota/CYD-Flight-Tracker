#include "main/net_rollback.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "admin/adapters/web_server.h"
#include "admin/core/firmware/ota_update.h"
#include "admin/core/firmware/version.h"
#include "platform/core_link.h"
#include "platform/wifi_manager.h"

namespace {
bool appPendingVerify = false;
}  // namespace

void netRollbackBegin(bool pending) { appPendingVerify = pending; }

void markAppValidWhenStable() {
  if (!appPendingVerify) return;
  const WifiStatus ws = wifiManagerStatus();
  const bool reachable =
      (ws == WifiStatus::Connected && webServerIsActive()) || ws == WifiStatus::PortalActive;
  const bool uiAlive =
      !heartbeatStale(millis(), core_link::uiBeatMs.load(), kUiAliveLimitMs);
  if (shouldRestartForRollback(appPendingVerify, millis(), uiAlive)) {
    Serial.println("[ota] the display loop has stopped on a new image: restarting to roll back");
    Serial.flush();
    ESP.restart();
  }
  if (shouldMarkAppValid(appPendingVerify, millis(), reachable, uiAlive)) {
    esp_ota_mark_app_valid_cancel_rollback();
    appPendingVerify = false;
    Serial.printf("[ota] %s marked valid after %u s, rollback cancelled\n", firmwareVersion(),
                  static_cast<unsigned>(millis() / 1000));
  }
}
