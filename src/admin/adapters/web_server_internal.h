// web_server_internal — what the two halves of the settings page's web server
// share: web_server.cpp (the server, its login and origin checks, the page and
// the settings routes) and web_server_firmware.cpp (POST /update and the GitHub
// routes). Not for anything outside these two files.
//
// Device-only (WebServer). Core 0, like the rest of web_server.
#pragma once

#include <WebServer.h>

#include <cstdint>

#include "admin/adapters/web_server.h"
#include "admin/core/config_page.h"
#include "admin/core/firmware/ota_update.h"
#include "admin/core/web_auth.h"

namespace web_server_detail {

extern WebServer server;
extern uint32_t saveCount;
extern OtaObserver otaObserver;
extern OtaSession *ota;
extern bool restartPending;
extern uint32_t restartAtMs;

// The checks every route uses (see their definitions in web_server.cpp).
bool isMultipartRequest();
bool credentialsOk();
OriginCheck requestOrigin();
bool requireSameOrigin();
bool authorize();

void sendPage(ConfigPageModel model, int status = 200);

// web_server_firmware.cpp: the routes webServerBegin() registers.
void handleUpdateChunk();
void handleUpdateDone();
void handleUpdateCheck();
void handleUpdateInstall();
void handleUpdatePrerelease();

}  // namespace web_server_detail
