#include "wifi_manager.h"

WifiStatus deriveWifiStatus(bool wifi_connected, bool portal_active) {
  if (wifi_connected) {
    return WifiStatus::Connected;
  }
  if (portal_active) {
    return WifiStatus::PortalActive;
  }
  return WifiStatus::Connecting;
}

bool shouldRestartConnection(bool portal_was_active, bool portal_active_now, bool wifi_connected) {
  return portal_was_active && !portal_active_now && !wifi_connected;
}

const char *wifiStatusLabel(WifiStatus status) {
  switch (status) {
    case WifiStatus::Connecting:
      return "CONNECTING";
    case WifiStatus::Connected:
      return "CONNECTED";
    case WifiStatus::PortalActive:
      return "SETUP PORTAL ACTIVE";
    case WifiStatus::Disconnected:
    default:
      return "DISCONNECTED";
  }
}

#ifdef ARDUINO

#include <WiFi.h>
#include <WiFiManager.h>

namespace {
WiFiManager wm;
WifiStatus currentStatus = WifiStatus::Disconnected;
bool portalWasActive = false;
constexpr char kPortalApName[] = "CYD-Sky-Tracker-Setup";
constexpr uint16_t kPortalTimeoutS = 180;
}  // namespace

void wifiManagerBegin() {
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(kPortalTimeoutS);

  currentStatus = WifiStatus::Connecting;
  // autoConnect() tries saved credentials first. In non-blocking mode it
  // returns immediately and opens the captive portal in the background if
  // they don't work, rather than blocking here until the portal times out.
  if (wm.autoConnect(kPortalApName)) {
    currentStatus = WifiStatus::Connected;
  }
  portalWasActive = wm.getConfigPortalActive();
}

void wifiManagerLoop() {
  wm.process();

  bool portalActiveNow = wm.getConfigPortalActive();
  bool connected = WiFi.status() == WL_CONNECTED;

  // The portal timed out without ever connecting — WiFiManager doesn't
  // retry on its own, so without this the device would be stuck reporting
  // "Connecting" forever with no portal open and nothing actually trying
  // to connect (see CLAUDE.md review notes 1.3). Reopen it instead.
  if (shouldRestartConnection(portalWasActive, portalActiveNow, connected)) {
    wm.autoConnect(kPortalApName);
    portalActiveNow = wm.getConfigPortalActive();
    connected = WiFi.status() == WL_CONNECTED;
  }
  portalWasActive = portalActiveNow;

  currentStatus = deriveWifiStatus(connected, portalActiveNow);
}

WifiStatus wifiManagerStatus() { return currentStatus; }

std::string wifiManagerSsid() {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  return std::string(WiFi.SSID().c_str());
}

#endif  // ARDUINO
