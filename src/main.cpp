// CYD Sky Tracker — hardware bring-up + WiFi onboarding.
//
// Initializes LovyanGFX (configured via include/LGFX_CYD.hpp), loads the
// persisted config (config_store), and starts WiFiManager (wifi_manager).
// The screen shows the current WiFi state (connecting / connected + SSID /
// setup portal active) instead of Milestone 1's static title. No
// table_view/radar_view yet — those land once opensky_client has data to
// show.

#include <Arduino.h>

#include <string>

#include "LGFX_CYD.hpp"
#include "config_store.h"
#include "wifi_manager.h"

static LGFX tft;
static WifiStatus lastDrawnStatus = WifiStatus::Disconnected;
static std::string lastDrawnSsid;
static bool statusDrawn = false;

static void drawStatus(WifiStatus status, const std::string &ssid) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(middle_center);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("CYD Sky Tracker", tft.width() / 2, tft.height() / 2 - 16);

  std::string label = wifiStatusLabel(status);
  if (status == WifiStatus::Connected && !ssid.empty()) {
    label += ": ";
    label += ssid;
  }

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString(label.c_str(), tft.width() / 2, tft.height() / 2 + 16);
}

void setup() {
  tft.init();
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(TFT_BLACK);

  // Not consumed yet — later milestones (opensky_client, config_portal)
  // will read home position / radius / poll interval / OpenSky credentials
  // from here. Loading it now exercises config_store on real NVS.
  Config cfg = loadConfig();
  (void)cfg;

  wifiManagerBegin();

  lastDrawnStatus = wifiManagerStatus();
  lastDrawnSsid = wifiManagerSsid();
  drawStatus(lastDrawnStatus, lastDrawnSsid);
  statusDrawn = true;
}

void loop() {
  wifiManagerLoop();

  WifiStatus status = wifiManagerStatus();
  std::string ssid = wifiManagerSsid();
  if (!statusDrawn || status != lastDrawnStatus || ssid != lastDrawnSsid) {
    drawStatus(status, ssid);
    lastDrawnStatus = status;
    lastDrawnSsid = ssid;
    statusDrawn = true;
  }
}
