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
}

void wifiManagerLoop() {
  wm.process();
  currentStatus = deriveWifiStatus(WiFi.status() == WL_CONNECTED, wm.getConfigPortalActive());
}

WifiStatus wifiManagerStatus() { return currentStatus; }

std::string wifiManagerSsid() {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  return std::string(WiFi.SSID().c_str());
}

#endif  // ARDUINO
