// wifi_manager — WiFi connect + captive-portal fallback via tzapu/WiFiManager.
// No display or OpenSky networking logic here (see CLAUDE.md "Code
// conventions") — this module only knows how to get the ESP32 onto a WiFi
// network and exposes a status enum for whatever draws it.
#pragma once

#include <string>

// WiFi connection state as understood by the rest of the firmware.
enum class WifiStatus {
  Connecting,
  Connected,
  PortalActive,
  Disconnected,
};

// Pure state-derivation logic, factored out of the WiFiManager/WiFi calls so
// it can run under `pio test -e native` — see CLAUDE.md "Testing".
WifiStatus deriveWifiStatus(bool wifi_connected, bool portal_active);

// Human-readable label for an LCARS-style status readout. Pure — no
// hardware calls — testable natively.
const char *wifiStatusLabel(WifiStatus status);

// --- Hardware adapter: drives the actual WiFiManager/WiFi calls. Thin
// wrapper around a hardware/SDK API — see CLAUDE.md "Testing" — not covered
// by Unity.

// Starts WiFiManager: tries stored WiFi credentials, and if none work opens
// a non-blocking captive-portal AP ("CYD-Sky-Tracker-Setup") so the user can
// pick a network from a phone/laptop. Call once from setup().
void wifiManagerBegin();

// Services the captive portal (DNS/HTTP) and refreshes the connection
// status. Call every loop() iteration — non-blocking, per CLAUDE.md's
// millis()-based timing rule.
void wifiManagerLoop();

WifiStatus wifiManagerStatus();

// SSID of the network currently associated with, or "" when not connected
// (e.g. still connecting, or the captive portal is active).
std::string wifiManagerSsid();
