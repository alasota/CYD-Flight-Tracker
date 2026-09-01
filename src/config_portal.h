// config_portal — local web config page for lat/lon/radius/poll_interval/
// OpenSky client_id/client_secret, reachable via mDNS at cyd-sky.local (see
// CLAUDE.md "Code conventions"). Submissions are persisted through
// config_store (Milestone 2).
#pragma once

#include <string>

// Area (in square degrees) of the lat/lon bounding box implied by a
// ±radius_deg half-width scan radius (i.e. a 2*radius_deg x 2*radius_deg
// box), matching config_store's Config::radius_deg. Pure — no WebServer/
// Arduino dependency — tested under `pio test -e native`.
float bboxAreaSqDeg(float radius_deg);

// OpenSky /states/all credit cost per call for that bbox area, per
// CLAUDE.md "Rate limits / credits": <=25 sq deg = 1, <=100 = 2, <=400 = 3,
// >400 = 4. Pure — tested under `pio test -e native`.
int openSkyCreditCost(float radius_deg);

// Parsed OpenSky OAuth client credentials from the JSON file OpenSky's
// account page generates for download: {"clientId":"...","clientSecret":
// "..."}.
struct OpenSkyCredentials {
  bool ok = false;  // true only if both fields were present and non-empty
  std::string client_id;
  std::string client_secret;
};

// Hard ceiling on how large a credentials JSON body
// parseOpenSkyCredentialsJson() will attempt to parse — matches the config
// page's own upload size cap, but enforced here too so the function is
// self-defending regardless of caller (see CLAUDE.md review notes 4.3).
constexpr size_t kMaxCredentialsJsonBytes = 4096;

// Parses that JSON file's contents (uploaded via the config page — see
// below). A body over kMaxCredentialsJsonBytes is rejected outright
// without being parsed. Pure — uses ArduinoJson but no WebServer/hardware
// dependency — tested under `pio test -e native`.
OpenSkyCredentials parseOpenSkyCredentialsJson(const std::string &json);

// True if `s` parses entirely as a valid (base-10) floating point number —
// no trailing garbage, not empty (leading whitespace is tolerated, same as
// strtof()). Used by the config form to reject malformed lat/lon/radius
// input instead of silently treating it as 0, which sanitizeConfig would
// then just clamp up to a "valid-looking" value with no feedback to the
// user — see CLAUDE.md review notes 1.6. Pure — tested under
// `pio test -e native`.
bool isValidFloatString(const std::string &s);

// True if `s` is a valid non-negative base-10 integer (digits only, at
// least one digit) — same idea as isValidFloatString(), for the
// poll-interval field.
bool isValidUnsignedIntString(const std::string &s);

// --- Hardware adapter: serves the local config web page at cyd-sky.local
// (mDNS) with a form for lat/lon/radius/poll_interval/OpenSky credentials,
// read/persisted via config_store. The radius field's credit cost updates
// live in the browser (client-side JS mirroring openSkyCreditCost() above)
// as the user types, no round trip needed. A second form lets the user
// upload OpenSky's credentials JSON file directly instead of copy-pasting
// clientId/clientSecret by hand (parsed via parseOpenSkyCredentialsJson()
// above). Thin wrapper around WebServer/ESPmDNS — not covered by Unity (see
// CLAUDE.md "Testing").

// Starts the mDNS responder ("cyd-sky.local") and the config web server.
// Call once WiFi is connected (e.g. the first loop() iteration where
// wifiManagerStatus() == WifiStatus::Connected) — mDNS/WebServer need an
// active station interface.
void configPortalBegin();

// Services pending HTTP requests. Safe to call every loop() iteration
// regardless of whether configPortalBegin() has run yet (no-op until it
// has) — non-blocking, per CLAUDE.md's millis()-based timing rule.
void configPortalLoop();

// True once configPortalBegin() has run.
bool configPortalIsActive();
