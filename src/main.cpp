// CYD Sky Tracker — hardware bring-up + WiFi onboarding + config portal +
// OpenSky polling.
//
// Initializes LovyanGFX (configured via include/LGFX_CYD.hpp), loads the
// persisted config (config_store), and starts WiFiManager (wifi_manager).
// Once WiFi connects, starts the local config web page (config_portal) at
// cyd-sky.local and begins polling OpenSky (opensky_client) on the
// configured interval. The screen still only shows WiFi/portal status (no
// table_view yet) — parsed Aircraft[] are printed to Serial each poll so
// opensky_client can be verified without a display.

#include <Arduino.h>

#include <string>

#include "LGFX_CYD.hpp"
#include "aircraft_lookup.h"
#include "config_portal.h"
#include "config_store.h"
#include "opensky_client.h"
#include "wifi_manager.h"

static LGFX tft;
static WifiStatus lastDrawnStatus = WifiStatus::Disconnected;
static std::string lastDrawnSsid;
static bool lastDrawnPortalActive = false;
static bool statusDrawn = false;

static void drawStatus(WifiStatus status, const std::string &ssid, bool portalActive) {
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

  if (portalActive) {
    tft.drawString("config: http://cyd-sky.local", tft.width() / 2, tft.height() / 2 + 32);
  }
}

static void printAircraft(const std::vector<Aircraft> &aircraft) {
  Serial.printf("[opensky] %u aircraft in range:\n", static_cast<unsigned>(aircraft.size()));
  for (const Aircraft &ac : aircraft) {
    if (ac.has_position) {
      Serial.printf("  %-6s %-8s lat=%.4f lon=%.4f alt=%.0fm v=%.0fm/s trk=%.0f\n",
                    ac.icao24.c_str(), ac.callsign.c_str(), ac.lat, ac.lon, ac.baro_altitude,
                    ac.velocity, ac.true_track);
    } else {
      Serial.printf("  %-6s %-8s (no position)\n", ac.icao24.c_str(), ac.callsign.c_str());
    }
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(TFT_BLACK);

  // Not otherwise consumed here — config_portal reads/writes it itself per
  // web request, and opensky_client reads it once per poll.
  Config cfg = loadConfig();
  (void)cfg;

  wifiManagerBegin();

  // TEMP: manual cache test, remove after
  /*
  Serial.println("=== aircraft_lookup manual cache test ===");
  AircraftInfo lookup1 = lookupAircraft("4010EE");
  Serial.printf("1st call: found=%s airline=\"%s\" type=\"%s\"\n",
                lookup1.found ? "true" : "false", lookup1.airline.c_str(),
                lookup1.aircraft_type.c_str());
  AircraftInfo lookup2 = lookupAircraft("4010EE");
  Serial.printf("2nd call: found=%s airline=\"%s\" type=\"%s\"\n",
                lookup2.found ? "true" : "false", lookup2.airline.c_str(),
                lookup2.aircraft_type.c_str());
  Serial.println("=== end aircraft_lookup manual cache test ===");
*/

  lastDrawnStatus = wifiManagerStatus();
  lastDrawnSsid = wifiManagerSsid();
  lastDrawnPortalActive = configPortalIsActive();
  drawStatus(lastDrawnStatus, lastDrawnSsid, lastDrawnPortalActive);
  statusDrawn = true;
}

void loop() {
  wifiManagerLoop();

  WifiStatus status = wifiManagerStatus();
  // mDNS/WebServer/HTTPClient all need an active station interface — start
  // the config portal and OpenSky polling only once WiFi first connects.
  if (status == WifiStatus::Connected) {
    if (!configPortalIsActive()) {
      configPortalBegin();
    }

    std::vector<Aircraft> aircraft;
    if (openSkyClientPoll(millis(), &aircraft)) {
      printAircraft(aircraft);
    }
  }
  configPortalLoop();

  std::string ssid = wifiManagerSsid();
  bool portalActive = configPortalIsActive();
  if (!statusDrawn || status != lastDrawnStatus || ssid != lastDrawnSsid ||
      portalActive != lastDrawnPortalActive) {
    drawStatus(status, ssid, portalActive);
    lastDrawnStatus = status;
    lastDrawnSsid = ssid;
    lastDrawnPortalActive = portalActive;
    statusDrawn = true;
  }
}
