// github_http — the device half of release_check: the daily HTTPS check of
// GitHub's releases API (verified TLS with github_ca.h's roots), the asked-for
// download of cyd-sky-tracker-<tag>.bin into ota_flash's OtaSession, and the
// state the config page and the display read. Hardware adapter, not covered by
// Unity (CLAUDE.md "Testing"); the decisions are release_check's pure functions.
#include "admin/core/firmware/release_check.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "admin/adapters/github_ca.h"
#include "admin/core/config_store.h"
#include "admin/core/firmware/version.h"
#include "platform/net_config.h"
#include "platform/time_sync.h"

namespace {

UpdateState state;
Config cfg;
bool begun = false;
uint32_t nextCheckAtMs = 0;
ManualCheck manualCheck;  // "check now" / the development switch, run from releaseCheckLoop()
std::string etagLatest;  // RAM only: a 304 answer doesn't count against GitHub's limit
std::string etagList;
std::string pendingInstallTag;
OtaObserver observer;
bool otaScreenShown = false;  // observer.onStart() ran for the current install

// Each answer GitHub gave to an API call.
struct ApiAnswer {
  int code = 0;  // HTTP status, or HTTPClient's negative error
  bool parsed = false;
  std::string etag, remaining, reset, retryAfter;
};

void prepare(WiFiClientSecure &client, HTTPClient &http) {
  client.setCACert(kGithubRootCAs);
  client.setHandshakeTimeout(kGithubHandshakeTimeoutS);
  http.useHTTP10(true);  // no chunked encoding: JSON / the image are read straight off the socket
  http.setConnectTimeout(kGithubConnectTimeoutMs);
  http.setTimeout(kGithubHttpTimeoutMs);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // every hop is checked (isAllowedUpdateUrl)
  http.setUserAgent(String("cyd-sky-tracker/") + firmwareVersion());
}

void logTlsError(WiFiClientSecure &client, const char *what) {
  char buf[96];
  const int err = client.lastError(buf, sizeof(buf));
  if (err != 0) {
    Serial.printf("[update] %s: TLS error %d (%s)\n", what, err, buf);
    state.lastError = err;
  }
}

ApiAnswer apiGetOnce(const std::string &url, const std::string &etag, bool list,
                     JsonDocument &doc) {
  ApiAnswer a;
  if (!isAllowedUpdateUrl(url)) {
    Serial.printf("[update] refusing %s\n", url.c_str());
    a.code = -1000;
    return a;
  }
  WiFiClientSecure client;
  HTTPClient http;
  prepare(client, http);
  if (!http.begin(client, url.c_str())) {
    a.code = -1;
    return a;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  if (!etag.empty()) http.addHeader("If-None-Match", etag.c_str());
  const char *keep[] = {"ETag", "X-RateLimit-Remaining", "X-RateLimit-Reset", "Retry-After"};
  http.collectHeaders(keep, 4);

  a.code = http.GET();
  if (a.code <= 0) {
    logTlsError(client, "api");
    http.end();
    return a;
  }
  a.etag = http.header("ETag").c_str();
  a.remaining = http.header("X-RateLimit-Remaining").c_str();
  a.reset = http.header("X-RateLimit-Reset").c_str();
  a.retryAfter = http.header("Retry-After").c_str();
  if (a.code == 200) {
    JsonDocument filter;
    buildReleaseFilter(filter, list);
    const DeserializationError e =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    a.parsed = e == DeserializationError::Ok;
    if (!a.parsed) Serial.printf("[update] bad JSON from GitHub: %s\n", e.c_str());
  }
  http.end();
  return a;
}

// A connection that fails before any answer (seen on the board: the TCP
// connect timing out now and then while the web panel is busy) gets one
// more try — a refused URL or any HTTP answer doesn't.
constexpr uint32_t kRetryPauseMs = 500;

ApiAnswer apiGet(const std::string &url, const std::string &etag, bool list, JsonDocument &doc) {
  ApiAnswer a = apiGetOnce(url, etag, list, doc);
  if (a.code <= 0 && a.code != -1000) {
    Serial.printf("[update] api: no answer (%d), trying once more\n", a.code);
    delay(kRetryPauseMs);
    doc.clear();
    a = apiGetOnce(url, etag, list, doc);
  }
  return a;
}

std::string apiBase() { return std::string("https://api.github.com/repos/") + kUpdateRepo; }

void refreshStatusFromLatest() {
  if (state.latest.tag.empty()) return;
  state.status = statusForRelease(decideOffer(state.installed, state.latest.tag), state.latest);
}

// One check: /releases/latest, and the /releases list when development
// versions are shown. Updates `state`; returns the outcome of the first.
CheckOutcome runCheck(bool useEtag) {
  const uint32_t t0 = millis();
  state.lastError = 0;
  JsonDocument doc;
  const ApiAnswer a = apiGet(apiBase() + "/releases/latest", useEtag ? etagLatest : "", false, doc);
  CheckOutcome o = a.code <= 0 ? CheckOutcome::Failed : classifyCheckResponse(a.code, a.remaining);
  if (o == CheckOutcome::Ok && !a.parsed) o = CheckOutcome::Failed;

  switch (o) {
    case CheckOutcome::Ok: {
      ReleaseInfo info;
      if (releaseFromJson(doc.as<JsonVariantConst>(), firmwareBoard(), info) && !info.prerelease) {
        state.latest = info;
        etagLatest = a.etag;
        refreshStatusFromLatest();
      } else {
        o = CheckOutcome::Failed;
        state.status = UpdateStatus::CheckFailed;
      }
      break;
    }
    case CheckOutcome::NotModified:
      refreshStatusFromLatest();
      break;
    case CheckOutcome::NoRelease:
      state.latest = ReleaseInfo();
      etagLatest.clear();
      state.status = UpdateStatus::NoRelease;
      break;
    case CheckOutcome::RateLimited:
      state.status = UpdateStatus::RateLimited;
      break;
    case CheckOutcome::Failed:
    default:
      state.status = UpdateStatus::CheckFailed;
      if (state.lastError == 0) state.lastError = a.code;
      break;
  }
  if (o != CheckOutcome::Failed && isTimeSynced()) state.checkedAt = timeSyncNowLocal();

  // Development channel: only for the admin who asked for it.
  if (!cfg.update_show_prerelease) {
    state.latestPre = ReleaseInfo();
    etagList.clear();
  } else if (o != CheckOutcome::Failed && o != CheckOutcome::RateLimited) {
    JsonDocument list;
    const ApiAnswer b =
        apiGet(apiBase() + "/releases?per_page=10", useEtag ? etagList : "", true, list);
    if (b.code == 200 && b.parsed) {
      ReleaseInfo pre;
      if (pickLatestPrerelease(list.as<JsonVariantConst>(), firmwareBoard(), pre) &&
          preReleaseWorthShowing(pre, state.latest)) {
        state.latestPre = pre;
      } else {
        state.latestPre = ReleaseInfo();
      }
      etagList = b.etag;
    } else if (b.code != 304) {
      Serial.printf("[update] development versions: http=%d\n", b.code);
    }
  }

  const uint32_t waitS = rateLimitWaitS(a.retryAfter, a.reset, time(nullptr));
  nextCheckAtMs = millis() + nextCheckDelayMs(o, waitS);
  Serial.printf("[update] check: http=%d latest=%s (installed %s) status=%d pre=%s, %u ms, "
                "next in %u min, free heap %u, stack left %u\n",
                a.code, state.latest.tag.c_str(), state.installed.c_str(),
                static_cast<int>(state.status), state.latestPre.tag.c_str(),
                static_cast<unsigned>(millis() - t0),
                static_cast<unsigned>(nextCheckDelayMs(o, waitS) / 60000),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return o;
}

// GET `url`, following redirects by hand: every hop has to pass
// isAllowedUpdateUrl() before anything connects to it. Leaves `http` open on
// the final answer; returns its status (or a negative error).
int openFollowingRedirects(WiFiClientSecure &client, HTTPClient &http, std::string url) {
  static const char *keep[] = {"Location"};
  bool retried = false;
  for (int hop = 0; hop <= kMaxUpdateRedirects; ++hop) {
    if (!isAllowedUpdateUrl(url)) {
      Serial.printf("[update] refusing to connect to %.80s\n", url.c_str());
      return -1000;
    }
    http.end();
    client.stop();
    prepare(client, http);
    if (!http.begin(client, url.c_str())) return -1;
    http.collectHeaders(keep, 1);
    const int code = http.GET();
    if (code <= 0) {
      logTlsError(client, "download");
      if (retried) return code;
      retried = true;  // same hop once more (see apiGet())
      --hop;
      delay(kRetryPauseMs);
      continue;
    }
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      url = http.header("Location").c_str();
      continue;
    }
    return code;
  }
  return -1001;  // too many redirects
}

// SHA256SUMS.txt of the release (a few hundred bytes) -> the hash of `name`.
std::string fetchSumFor(const std::string &sumsUrl, const std::string &name) {
  WiFiClientSecure client;
  HTTPClient http;
  std::string hash;
  if (openFollowingRedirects(client, http, sumsUrl) == 200 && http.getSize() > 0 &&
      http.getSize() <= 4096) {
    hash = findSha256InSums(http.getString().c_str(), name);
  }
  http.end();
  return hash;
}

OtaResult download(const ReleaseInfo &r, const std::string &sha256) {
  static uint8_t buf[4096];  // static: not on net_task's stack
  WiFiClientSecure client;
  HTTPClient http;
  const int code = openFollowingRedirects(client, http, r.assetUrl);
  const int size = http.getSize();
  if (code != 200 || size <= 0 || static_cast<size_t>(size) != r.assetSize) {
    Serial.printf("[update] download: http=%d size=%d (expected %u)\n", code, size,
                  static_cast<unsigned>(r.assetSize));
    http.end();
    return OtaResult::DownloadFailed;
  }

  OtaSession &session = otaFlashSession();
  OtaExpectations expect;
  expect.sha256 = sha256;
  expect.version = r.tag;
  session.start(static_cast<size_t>(size), expect);
  if (session.failed()) {
    http.end();
    return session.finish();
  }
  if (observer.onStart != nullptr) observer.onStart(static_cast<size_t>(size));
  otaScreenShown = true;

  WiFiClient *stream = http.getStreamPtr();
  const uint32_t startMs = millis();
  uint32_t lastDataMs = startMs;
  size_t got = 0, notified = 0;
  while (got < static_cast<size_t>(size) && !session.failed()) {
    const int avail = stream->available();
    if (avail > 0) {
      const int n = stream->read(buf, avail < static_cast<int>(sizeof(buf)) ? avail : sizeof(buf));
      if (n > 0) {
        session.feed(buf, static_cast<size_t>(n));
        got += static_cast<size_t>(n);
        lastDataMs = millis();
        if (observer.onProgress != nullptr && got - notified >= 16 * 1024) {
          notified = got;
          observer.onProgress(got, static_cast<size_t>(size), session.marker().version().c_str());
        }
      }
    } else {
      if (!stream->connected()) break;
      if (millis() - lastDataMs > kGithubDownloadStallMs) break;
      delay(2);
    }
    if (millis() - startMs > kGithubDownloadTotalMs) break;
  }
  http.end();
  if (!session.failed() && got != static_cast<size_t>(size)) {
    session.abort();
    Serial.printf("[update] download broke off at %u of %d bytes\n", static_cast<unsigned>(got),
                  size);
    return OtaResult::DownloadFailed;
  }
  const OtaResult result = session.finish();
  Serial.printf("[update] install %s: %u B in %u s -> result %d %s (stack left %u)\n",
                r.tag.c_str(), static_cast<unsigned>(got),
                static_cast<unsigned>((millis() - startMs) / 1000), static_cast<int>(result),
                result == OtaResult::VerifyFailed || result == OtaResult::WriteFailed
                    ? otaFlashError()
                    : "",
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return result;
}

void runInstall(const std::string &tag) {
  Serial.printf("[update] installing %s, free heap %u, largest block %u\n", tag.c_str(),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
  // Fresh URLs, size and hash — a check from hours ago may be stale.
  runCheck(/*useEtag=*/false);
  ReleaseInfo r;
  if (tag == state.latest.tag && statusAllowsInstall(state.status)) {
    r = state.latest;
  } else if (cfg.update_show_prerelease && tag == state.latestPre.tag) {
    r = state.latestPre;
  } else {
    Serial.printf("[update] %s is no longer on offer (latest %s), not installing\n", tag.c_str(),
                  state.latest.tag.c_str());
    return;
  }

  std::string sha = r.sha256;
  if (sha.empty() && !r.sumsUrl.empty()) sha = fetchSumFor(r.sumsUrl, releaseAssetName(r.tag, firmwareBoard()));
  if (sha.empty()) {
    Serial.printf("[update] %s has no SHA-256, not installing\n", tag.c_str());
    state.status = UpdateStatus::NoChecksum;
    return;
  }

  otaScreenShown = false;
  const OtaResult result = download(r, sha);
  const bool ok = result == OtaResult::Ok;
  if (otaScreenShown && observer.onEnd != nullptr) observer.onEnd(ok);
  if (ok) {
    Serial.printf("[update] %s installed (sha256 %.12s…), restarting\n", tag.c_str(), sha.c_str());
    Serial.flush();
    delay(1500);  // "done · restarting" stays readable for a moment
    ESP.restart();
  }
  state.lastInstall = result;
  state.lastInstallTag = tag;
}

}  // namespace

void releaseCheckBegin(const Config &config) {
  const bool preWas = cfg.update_show_prerelease;
  cfg = config;
  state.installed = firmwareVersion();
  if (!begun) {
    begun = true;
    nextCheckAtMs = millis() + kUpdateFirstCheckAfterMs;
  }
  if (cfg.update_show_prerelease != preWas) {
    etagList.clear();
    if (!cfg.update_show_prerelease) state.latestPre = ReleaseInfo();
  }
  if (!cfg.update_check_enabled && state.status == UpdateStatus::NeverChecked) {
    state.status = UpdateStatus::Disabled;
  } else if (cfg.update_check_enabled && state.status == UpdateStatus::Disabled) {
    state.status = UpdateStatus::NeverChecked;
  }
}

void releaseCheckLoop(const CheckGate &gate) {
  if (!begun) return;
  if (!pendingInstallTag.empty()) {
    CheckGate g = gate;
    g.enabled = true;               // asked for by hand
    g.msUntilNextPoll = UINT32_MAX;  // the user is waiting; OpenSky can wait
    if (canStartNetworkTask(g)) {
      const std::string tag = pendingInstallTag;
      pendingInstallTag.clear();
      runInstall(tag);
    }
    return;
  }
  // "Check now": here, after the poll — not in the page handler, which may run
  // inside phase B next to the hexdb.io TLS session (spec 09).
  CheckGate manual = gate;
  manual.enabled = true;               // asked for by hand
  manual.msUntilNextPoll = UINT32_MAX;  // the user is waiting; OpenSky can wait
  if (takeManualCheck(manualCheck, canStartNetworkTask(manual), millis())) {
    runCheck(/*useEtag=*/true);
    return;
  }
  if (static_cast<int32_t>(millis() - nextCheckAtMs) < 0) return;
  if (canStartNetworkTask(gate)) runCheck(/*useEtag=*/true);
}

ManualCheckAsk releaseCheckRequest() { return askManualCheck(manualCheck, millis()); }

bool releaseCheckPending() { return manualCheckPending(manualCheck, millis()); }

bool releaseInstallRequest(const std::string &tag) {
  if (!pendingInstallTag.empty() || tag.empty() || tag == state.installed) return false;
  const bool release = tag == state.latest.tag && statusAllowsInstall(state.status);
  const bool pre = cfg.update_show_prerelease && tag == state.latestPre.tag &&
                   preReleaseWorthShowing(state.latestPre, state.latest);
  if (!release && !pre) return false;
  pendingInstallTag = tag;
  return true;
}

bool releaseInstallPending() { return !pendingInstallTag.empty(); }

const UpdateState &releaseUpdateState() { return state; }

void releaseSetOtaObserver(const OtaObserver &o) { observer = o; }
