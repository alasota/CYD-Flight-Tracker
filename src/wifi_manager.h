// wifi_manager — WiFi connect + captive-portal fallback via tzapu/WiFiManager.
// No display or OpenSky networking logic here (see CLAUDE.md "Code
// conventions") — this module only knows how to get the ESP32 onto a WiFi
// network and exposes a status enum for whatever draws it.
#pragma once

#include <cstdint>
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

// True if the connection attempt should be restarted: the captive portal
// was open a moment ago and has just closed (timed out) without a
// successful connection. Without this, the device gets stuck reporting
// "Connecting" forever with nothing actually trying to connect and no
// portal to reconfigure it from — see CLAUDE.md review notes 1.3. Pure —
// tested under `pio test -e native`.
bool shouldRestartConnection(bool portal_was_active, bool portal_active_now, bool wifi_connected);

// Grace period after losing the link before the firmware nudges a
// reconnect itself, and the minimum gap between those nudges. The ESP32
// WiFi core auto-reconnects on its own; this is a backstop for when it
// gives up or stalls (see CLAUDE.md review notes 1.5).
constexpr uint32_t kReconnectGraceMs = 10UL * 1000UL;
constexpr uint32_t kReconnectIntervalMs = 15UL * 1000UL;

// True when the firmware should call WiFi.reconnect() now: not connected,
// no captive portal open (the portal owns the radio while it's up), the
// link has been down longer than kReconnectGraceMs, and it's been at
// least kReconnectIntervalMs since the last nudge. Pure — the two elapsed
// times are injected — native-tested.
bool shouldAttemptReconnect(bool wifi_connected, bool portal_active, uint32_t ms_since_disconnected,
                            uint32_t ms_since_last_attempt);

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
