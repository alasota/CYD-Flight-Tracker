// web_server — local web config page for lat/lon/radius/poll_interval/
// OpenSky client_id/client_secret/auto-cycle settings, reachable via mDNS
// at cyd-sky.local (see
// CLAUDE.md "Code conventions"). Submissions are persisted through
// config_store.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "admin/core/firmware/ota_update.h"  // OtaObserver

// --- Hardware adapter: serves the local config web page at cyd-sky.local
// (mDNS) with a form for lat/lon/radius/poll interval/auto-cycle/OpenSky
// credentials (and the rest of Config's settings objects),
// read/persisted via config_store. The radius field's credit cost updates
// live in the browser (client-side JS mirroring scan_area's openSkyCreditCost())
// as the user types, no round trip needed. A second form lets the user
// upload OpenSky's credentials JSON file directly instead of copy-pasting
// clientId/clientSecret by hand (parsed by settings_form's
// parseOpenSkyCredentialsJson()). Thin wrapper around WebServer/ESPmDNS — not covered by Unity (see
// CLAUDE.md "Testing").

// Starts the mDNS responder ("cyd-sky.local") and the config web server.
// Call once WiFi is connected (e.g. the first loop() iteration where
// wifiManagerStatus() == WifiStatus::Connected) — mDNS/WebServer need an
// active station interface.
void webServerBegin();

// Services pending HTTP requests. Safe to call every loop() iteration
// regardless of whether webServerBegin() has run yet (no-op until it
// has) — non-blocking, per CLAUDE.md's millis()-based timing rule.
void webServerLoop();

// True once webServerBegin() has run.
bool webServerIsActive();

// Incremented every time the settings are saved (settings form or
// credentials upload). net_task compares it against the value it last saw
// to reload the config straight away — e.g. so a language change shows up
// on screen immediately rather than at the next OpenSky poll.
uint32_t webServerSaveCount();

// Where GET /screenshot.bmp gets its pixels — supplied by the UI, so this
// module stays free of display code. The display belongs to the UI core
// (spec/done/04-dual-core.md §5.7), so these are requests to it, made from the web
// handler on the network core:
//   begin(screen)  the UI switches to `screen` (-1 = keep) and stops drawing
//                  until end(); false = the UI didn't answer in `timeoutMs`
//   requestBand    the UI fills `rows` BMP rows of `stride` bytes into `out`,
//                  bottom row of the band first (BMP is bottom-up)
//   waitBand       ... and this waits for it; false on timeout — the request
//                  is then invalidated, the UI never writes into `out` again,
//                  so it may be freed
//   end()          drawing resumes; a band still outstanding is invalidated too
// requestBand returns at once, so the next band is read off the panel while
// the previous one goes out over TCP. Until set, the endpoint answers 503.
struct ScreenshotSource {
  int width = 0;
  int height = 0;
  int screenCount = 0;
  bool (*begin)(int screenIndex, uint32_t timeoutMs) = nullptr;
  void (*requestBand)(int y0, int rows, uint32_t stride, uint8_t *out) = nullptr;
  bool (*waitBand)(uint32_t timeoutMs) = nullptr;
  void (*end)() = nullptr;
};
void webServerSetScreenshotSource(const ScreenshotSource &source);

// POST /update (firmware over WiFi, ota_update) reports through this
// OtaObserver; its callbacks run inside webServerLoop() (the upload is
// handled there in one go).
void webServerSetOtaObserver(const OtaObserver &observer);

// True between a successful update and the restart it triggers — net_task
// stays off the network meanwhile, and the UI keeps the OTA service screen
// up (it sees this as NetState::restartPending).
bool webServerRestartPending();


// GET /perf.json — perf_stats' JSON, supplied by net_task (503 until set).
using PerfJsonSource = std::string (*)();
void webServerSetPerfSource(PerfJsonSource source);
