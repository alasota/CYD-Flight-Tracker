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
#include "table_view.h"
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

// TEMP: fake data for visual check, remove in step 8
// 6 made-up rows: different airlines/types/distances, one very close
// (should get table_view's highlight), and one with an empty
// airline/type simulating a missed aircraft_lookup (hexdb.io 404).
static std::vector<AircraftRow> buildFakeAircraftRows() {
  std::vector<AircraftRow> rows;

  AircraftRow lot;
  lot.aircraft.icao24 = "3c6444";
  lot.aircraft.callsign = "LOT281";
  lot.aircraft.baro_altitude = 1200.0f;
  lot.aircraft.velocity = 85.0f;
  lot.aircraft.true_track = 270.0f;
  lot.aircraft.has_position = true;
  lot.info.found = true;
  lot.info.airline = "LOT Polish Airlines";
  lot.info.aircraft_type = "737 MAX 8";
  lot.distance_km = 0.8f;  // very close — should get the highlight
  lot.has_distance = true;
  rows.push_back(lot);

  AircraftRow dlh;
  dlh.aircraft.icao24 = "4010ee";
  dlh.aircraft.callsign = "DLH9LH";
  dlh.aircraft.baro_altitude = 9800.0f;
  dlh.aircraft.velocity = 230.0f;
  dlh.aircraft.true_track = 95.0f;
  dlh.aircraft.has_position = true;
  dlh.info.found = true;
  dlh.info.airline = "Lufthansa";
  dlh.info.aircraft_type = "A320 214";
  dlh.distance_km = 15.0f;
  dlh.has_distance = true;
  rows.push_back(dlh);

  AircraftRow ryr;
  ryr.aircraft.icao24 = "406f01";
  ryr.aircraft.callsign = "RYR7HL";
  ryr.aircraft.baro_altitude = 10500.0f;
  ryr.aircraft.velocity = 245.0f;
  ryr.aircraft.true_track = 180.0f;
  ryr.aircraft.has_position = true;
  ryr.info.found = true;
  ryr.info.airline = "Ryanair";
  ryr.info.aircraft_type = "737-800";
  ryr.distance_km = 42.0f;
  ryr.has_distance = true;
  rows.push_back(ryr);

  // Simulates a missed aircraft_lookup (icao24 not in hexdb.io) — airline/
  // type stay empty; table_view should render "--" instead of hiding it.
  AircraftRow unknown;
  unknown.aircraft.icao24 = "a1b2c3";
  unknown.aircraft.callsign = "N12345";
  unknown.aircraft.baro_altitude = 3500.0f;
  unknown.aircraft.velocity = 120.0f;
  unknown.aircraft.true_track = 45.0f;
  unknown.aircraft.has_position = true;
  unknown.info.found = false;
  unknown.distance_km = 28.0f;
  unknown.has_distance = true;
  rows.push_back(unknown);

  AircraftRow ual;
  ual.aircraft.icao24 = "a835af";
  ual.aircraft.callsign = "UAL934";
  ual.aircraft.baro_altitude = 11200.0f;
  ual.aircraft.velocity = 250.0f;
  ual.aircraft.true_track = 310.0f;
  ual.aircraft.has_position = true;
  ual.info.found = true;
  ual.info.airline = "United Airlines";
  ual.info.aircraft_type = "777-200";
  ual.distance_km = 88.0f;
  ual.has_distance = true;
  rows.push_back(ual);

  AircraftRow ezy;
  ezy.aircraft.icao24 = "471f5b";
  ezy.aircraft.callsign = "EZY62KX";
  ezy.aircraft.baro_altitude = 9200.0f;
  ezy.aircraft.velocity = 215.0f;
  ezy.aircraft.true_track = 120.0f;
  ezy.aircraft.has_position = true;
  ezy.info.found = true;
  ezy.info.airline = "easyJet";
  ezy.info.aircraft_type = "A319 111";
  ezy.distance_km = 120.0f;
  ezy.has_distance = true;
  rows.push_back(ezy);

  return rows;
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
  // the config portal only once WiFi first connects.
  if (status == WifiStatus::Connected && !configPortalIsActive()) {
    configPortalBegin();
  }
  configPortalLoop();

  // Real polling — disabled for the table_view visual check below.
  // TEMP: fake data for visual check, remove in step 8 (restore this).
  // if (status == WifiStatus::Connected) {
  //   std::vector<Aircraft> aircraft;
  //   if (openSkyClientPoll(millis(), &aircraft)) {
  //     printAircraft(aircraft);
  //   }
  // }

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

  // TEMP: fake data for visual check, remove in step 8
  static bool fakeTableDrawn = false;
  if (status == WifiStatus::Connected && !fakeTableDrawn) {
    std::vector<AircraftRow> fakeRows = buildFakeAircraftRows();
    sortRowsByDistance(fakeRows);

    tft.fillScreen(TFT_BLACK);
    drawTablePage(tft, fakeRows, 0, 0, tft.width(), tft.height(), 0);
    fakeTableDrawn = true;
  }
}
