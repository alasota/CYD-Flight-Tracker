// CYD Sky Tracker — hardware bring-up.
//
// Minimal sanity check for the ESP32-2432S028R ("CYD") display wiring:
// initialize LovyanGFX (configured via include/LGFX_CYD.hpp) and draw a
// centered title. Nothing else lives here yet — this step is pure hardware
// bring-up with no testable logic, so there is no accompanying native test.
// Real logic (opensky_client and friends) starts test-first per CLAUDE.md's
// "Testing" section.

#include <Arduino.h>

#include "LGFX_CYD.hpp"

static LGFX tft;

void setup() {
  tft.init();
  tft.setRotation(1);  // landscape, 320x240
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(middle_center);
  tft.setTextSize(2);
  tft.drawString("CYD Sky Tracker", tft.width() / 2, tft.height() / 2);
}

void loop() {
  // Nothing to do yet.
}
