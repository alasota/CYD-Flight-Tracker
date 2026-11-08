// web_server_firmware — the settings page's firmware routes: POST /update (the
// file goes into the spare app slot through the shared flash session,
// ota_update) and the GitHub routes (release_check). Every POST starts with
// requireSameOrigin() then authorize(), every upload callback with
// isMultipartRequest(). See docs/architecture.md "Firmware updates".
#include "admin/adapters/web_server_internal.h"

#include <cstdlib>
#include <string>

#include "admin/core/firmware/release_check.h"
#include "admin/core/firmware/version.h"
#include "admin/core/settings.h"

namespace web_server_detail {

namespace {

bool otaAuthorized = false;
size_t otaLastNotified = 0;

}  // namespace

// POST /update — upload callback: streams the file into the spare app slot
// (OtaSession checks header, size and the Sky Tracker marker first). Without
// a valid login, or sent from another site, nothing at all is written;
// handleUpdateDone() answers 401 / 403.
void handleUpdateChunk() {
  if (!isMultipartRequest()) return;  // see isMultipartRequest()
  HTTPUpload &upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      otaAuthorized =
          decideUploadStart(requestOrigin(), credentialsOk()) == UploadStart::Accept;
      if (!otaAuthorized) return;
      const size_t declared =
          static_cast<size_t>(std::strtoul(server.arg("size").c_str(), nullptr, 10));
      ota->start(declared);
      otaLastNotified = 0;
      Serial.printf("[ota] upload '%s', %u bytes declared, free heap %u\n", upload.filename.c_str(),
                    static_cast<unsigned>(declared), static_cast<unsigned>(ESP.getFreeHeap()));
      if (!ota->failed() && otaObserver.onStart != nullptr) otaObserver.onStart(declared);
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (!otaAuthorized) return;
      ota->feed(upload.buf, upload.currentSize);
      // Repaint every 16 KB — often enough to look live, rare enough not to slow the upload.
      if (!ota->failed() && otaObserver.onProgress != nullptr &&
          ota->received() - otaLastNotified >= 16 * 1024) {
        otaLastNotified = ota->received();
        otaObserver.onProgress(ota->received(), ota->declaredBytes(),
                               ota->marker().version().c_str());
      }
      break;
    case UPLOAD_FILE_END:
      if (!otaAuthorized) return;
      ota->finish();
      break;
    case UPLOAD_FILE_ABORTED:
      if (!otaAuthorized) return;
      ota->abort();
      break;
    default:
      break;
  }
}

void handleUpdateDone() {
  // A refused upload (origin or password) wrote nothing; answer without
  // touching the OTA session, so the display shows no "update failed".
  if (!requireSameOrigin() || !authorize()) {
    otaAuthorized = false;
    return;
  }
  const bool started = ota->started() && otaAuthorized;
  const OtaResult r = started ? ota->finish() : OtaResult::NoFile;
  otaAuthorized = false;

  std::string detail;     // for the answer page
  std::string logDetail;  // the same, for the serial log (ASCII arrow)
  if (r == OtaResult::Ok) {
    detail = otaVersionChangePageText(firmwareVersion(), ota->marker().version());
    logDetail = otaVersionChangeLogText(firmwareVersion(), ota->marker().version());
  } else if (r == OtaResult::WriteFailed || r == OtaResult::VerifyFailed) {
    detail = otaFlashError();
    logDetail = detail;
  }
  Serial.printf("[ota] result=%d received=%u new=%s %s\n", static_cast<int>(r),
                static_cast<unsigned>(ota->received()), ota->marker().version().c_str(),
                logDetail.c_str());
  if (started && otaObserver.onEnd != nullptr) otaObserver.onEnd(r == OtaResult::Ok);

  ConfigPageModel model;
  model.cfg = loadSettings();
  model.tab = SettingsTab::Firmware;
  model.hasMessage = true;
  model.message = otaResultMessage(r);
  model.isError = r != OtaResult::Ok;
  model.messageDetail = detail;
  model.restarting = r == OtaResult::Ok;
  sendPage(model, otaResultHttpStatus(r));

  if (r == OtaResult::Ok) {
    restartPending = true;
    restartAtMs = millis();
  }
}

// ---- Updates from GitHub (release_check)

// "Check now" only queues the check: release_check runs it in net_task's loop
// after the poll. This handler may run inside phase B (betweenFetches), where
// the hexdb.io TLS session still holds its heap. Back to the Firmware tab,
// which says "checking…" and reloads until the answer is in — or why not.
void requestCheckAndAnswer() {
  if (releaseCheckRequest() == ManualCheckAsk::TooSoon) {
    ConfigPageModel model;
    model.cfg = loadSettings();
    model.tab = SettingsTab::Firmware;
    model.hasMessage = true;
    model.message = WebStr::UpdCheckTooSoon;
    sendPage(model);
    return;
  }
  server.sendHeader("Location", "/?tab=firmware");
  server.send(303, "text/plain", "");
}

// POST /update/check — "check now".
void handleUpdateCheck() {
  if (!requireSameOrigin() || !authorize()) return;
  requestCheckAndAnswer();
}

// POST /update/install (tag=...) — queues the install; the download starts in
// the next loop() pass, after this page has reached the browser.
void handleUpdateInstall() {
  if (!requireSameOrigin() || !authorize()) return;
  const std::string tag = server.arg("tag").c_str();
  if (!releaseInstallRequest(tag)) {
    ConfigPageModel model;
    model.cfg = loadSettings();
    model.tab = SettingsTab::Firmware;
    model.hasMessage = true;
    model.message = WebStr::UpdInstallRefused;
    model.isError = true;
    sendPage(model, 409);
    return;
  }
  Serial.printf("[update] install %s asked for on the config page\n", tag.c_str());
  ConfigPageModel model;
  model.cfg = loadSettings();
  model.tab = SettingsTab::Firmware;
  model.hasMessage = true;
  model.message = WebStr::UpdDownloading;
  model.messageDetail = tag;
  model.restarting = true;  // the page waits for the board and reloads itself
  sendPage(model);
}

// POST /update/prerelease (on=0|1) — "show development versions", saved on
// its own and followed by a fresh check.
void handleUpdatePrerelease() {
  if (!requireSameOrigin() || !authorize()) return;
  const bool on = server.arg("on") == "1";
  saveUpdateShowPrerelease(on);
  ++saveCount;  // net_task reloads the config (and the UI gets it via NetState)
  releaseCheckBegin(loadSettings().update);
  requestCheckAndAnswer();
}

}  // namespace web_server_detail
