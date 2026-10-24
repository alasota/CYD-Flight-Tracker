#include "release_check.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Reads a decimal number (1..5 digits, <= 65535) at `p`; advances `p`.
bool readNumber(const char *&p, uint16_t &out) {
  if (!std::isdigit(static_cast<unsigned char>(*p))) return false;
  uint32_t v = 0;
  int digits = 0;
  while (std::isdigit(static_cast<unsigned char>(*p))) {
    v = v * 10 + static_cast<uint32_t>(*p - '0');
    if (++digits > 5 || v > 65535) return false;
    ++p;
  }
  out = static_cast<uint16_t>(v);
  return true;
}

bool startsWith(const char *p, const char *prefix) {
  return std::strncmp(p, prefix, std::strlen(prefix)) == 0;
}

bool isHexString(const std::string &s, size_t len) {
  if (s.size() != len) return false;
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string lower(std::string s) {
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Only what is ever shown or compared: no quotes, angle brackets, spaces.
bool isPlainText(const std::string &s, const char *extra) {
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c))) continue;
    if (std::strchr(extra, c) == nullptr || c == '\0') return false;
  }
  return true;
}

}  // namespace

// ---- Versions

FwVersion parseFwVersion(const std::string &s) {
  FwVersion v;
  const char *p = s.c_str();
  if (*p != 'v') return v;
  ++p;
  if (!readNumber(p, v.major) || *p++ != '.') return v;
  if (!readNumber(p, v.minor) || *p++ != '.') return v;
  if (!readNumber(p, v.patch)) return v;

  // "-dev.N" / "-beta.N" / "-rc.N"
  if (*p == '-') {
    PreKind kind = PreKind::None;
    const char *q = p + 1;
    if (startsWith(q, "dev.")) {
      kind = PreKind::Dev;
      q += 4;
    } else if (startsWith(q, "beta.")) {
      kind = PreKind::Beta;
      q += 5;
    } else if (startsWith(q, "rc.")) {
      kind = PreKind::Rc;
      q += 3;
    }
    if (kind != PreKind::None) {
      if (!readNumber(q, v.preNumber)) return v;
      v.pre = kind;
      p = q;
    }
  }
  // git describe: "-<n>-g<hash>"
  if (*p == '-' && std::isdigit(static_cast<unsigned char>(p[1]))) {
    const char *q = p + 1;
    uint16_t n = 0;
    if (!readNumber(q, n) || q[0] != '-' || q[1] != 'g') return v;
    q += 2;
    const char *hashStart = q;
    while (std::isxdigit(static_cast<unsigned char>(*q))) ++q;
    if (q - hashStart < 4) return v;
    v.commitsAhead = n;
    p = q;
  }
  if (std::strcmp(p, "-dirty") == 0) {
    v.dirty = true;
    p += 6;
  }
  if (*p != '\0') return v;
  v.valid = true;
  return v;
}

bool isReleaseTagFormat(const std::string &tag) {
  const FwVersion v = parseFwVersion(tag);
  return v.valid && v.commitsAhead == 0 && !v.dirty;
}

int compareFwVersions(const FwVersion &a, const FwVersion &b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  if (a.pre != b.pre) return a.pre < b.pre ? -1 : 1;
  if (a.preNumber != b.preNumber) return a.preNumber < b.preNumber ? -1 : 1;
  return 0;
}

bool isDevelopmentBuild(const FwVersion &installed) {
  return !installed.valid || installed.pre != PreKind::None || installed.commitsAhead > 0 ||
         installed.dirty;
}

UpdateOffer decideOffer(const std::string &installed, const std::string &latestTag) {
  if (!isReleaseTagFormat(latestTag)) return UpdateOffer::BadLatest;
  const FwVersion latest = parseFwVersion(latestTag);
  const FwVersion inst = parseFwVersion(installed);
  if (!inst.valid || inst.dirty) return UpdateOffer::DevBuild;
  const int cmp = compareFwVersions(latest, inst);
  if (cmp > 0) return UpdateOffer::Newer;
  if (cmp < 0 && inst.pre != PreKind::None) return UpdateOffer::BackToRelease;
  return UpdateOffer::UpToDate;
}

// ---- Releases

std::string releaseAssetName(const std::string &tag, const std::string &board) {
  if (board.empty() || board == "cyd-r") return "cyd-sky-tracker-" + tag + ".bin";
  return "cyd-sky-tracker-" + board + "-" + tag + ".bin";
}

void buildReleaseFilter(JsonDocument &filter, bool list) {
  filter.clear();
  JsonObject r = list ? filter[0].to<JsonObject>() : filter.to<JsonObject>();
  r["tag_name"] = true;
  r["html_url"] = true;
  r["published_at"] = true;
  r["draft"] = true;
  r["prerelease"] = true;
  JsonObject a = r["assets"][0].to<JsonObject>();
  a["name"] = true;
  a["size"] = true;
  a["digest"] = true;
  a["browser_download_url"] = true;
}

bool releaseFromJson(JsonVariantConst release, const std::string &board, ReleaseInfo &out) {
  out = ReleaseInfo();
  if (!release.is<JsonObjectConst>()) return false;
  if (release["draft"].as<bool>()) return false;
  const std::string tag = release["tag_name"] | "";
  if (!isReleaseTagFormat(tag)) return false;
  out.tag = tag;
  out.prerelease = release["prerelease"].as<bool>();

  const std::string html = release["html_url"] | "";
  if (isAllowedUpdateUrl(html)) out.htmlUrl = html;
  const std::string published = release["published_at"] | "";
  if (isPlainText(published, "-:TZ.")) out.publishedAt = published;

  const std::string wanted = releaseAssetName(tag, board);
  for (JsonVariantConst a : release["assets"].as<JsonArrayConst>()) {
    const std::string name = a["name"] | "";
    const std::string url = a["browser_download_url"] | "";
    if (!isAllowedUpdateUrl(url)) continue;
    if (name == wanted) {
      out.assetUrl = url;
      out.assetSize = a["size"] | static_cast<size_t>(0);
      out.sha256 = parseAssetDigest(a["digest"] | "");
    } else if (name == "SHA256SUMS.txt") {
      out.sumsUrl = url;
    }
  }
  return true;
}

bool parseLatestRelease(const std::string &json, const std::string &board, ReleaseInfo &out) {
  JsonDocument filter;
  buildReleaseFilter(filter, /*list=*/false);
  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter)) !=
      DeserializationError::Ok) {
    out = ReleaseInfo();
    return false;
  }
  if (!releaseFromJson(doc.as<JsonVariantConst>(), board, out)) return false;
  if (out.prerelease) {
    out = ReleaseInfo();
    return false;
  }
  return true;
}

bool pickLatestPrerelease(JsonVariantConst list, const std::string &board, ReleaseInfo &out) {
  out = ReleaseInfo();
  bool found = false;
  FwVersion best;
  for (JsonVariantConst r : list.as<JsonArrayConst>()) {
    ReleaseInfo info;
    if (!releaseFromJson(r, board, info) || !info.prerelease || info.assetUrl.empty()) continue;
    const FwVersion v = parseFwVersion(info.tag);
    if (!found || compareFwVersions(v, best) > 0) {
      best = v;
      out = info;
      found = true;
    }
  }
  return found;
}

bool pickLatestPrerelease(const std::string &json, const std::string &board, ReleaseInfo &out) {
  JsonDocument filter;
  buildReleaseFilter(filter, /*list=*/true);
  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter)) !=
      DeserializationError::Ok) {
    out = ReleaseInfo();
    return false;
  }
  return pickLatestPrerelease(doc.as<JsonVariantConst>(), board, out);
}

std::string parseAssetDigest(const std::string &digest) {
  static const char kPrefix[] = "sha256:";
  if (digest.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return "";
  const std::string hex = digest.substr(sizeof(kPrefix) - 1);
  return isHexString(hex, 64) ? lower(hex) : "";
}

std::string findSha256InSums(const std::string &sums, const std::string &assetName) {
  size_t pos = 0;
  while (pos < sums.size()) {
    size_t eol = sums.find('\n', pos);
    if (eol == std::string::npos) eol = sums.size();
    std::string line = sums.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 66) continue;
    const std::string hex = line.substr(0, 64);
    if (!isHexString(hex, 64) || line[64] != ' ') continue;
    std::string name = line.substr(65);
    if (!name.empty() && (name[0] == ' ' || name[0] == '*')) name.erase(0, 1);
    if (name == assetName) return lower(hex);
  }
  return "";
}

std::string formatIsoDate(const std::string &iso) {
  // YYYY-MM-DD...
  if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return "";
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
    if (!std::isdigit(static_cast<unsigned char>(iso[static_cast<size_t>(i)]))) return "";
  }
  return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
}

// ---- Allowed URLs

bool isAllowedUpdateUrl(const std::string &url) {
  static const char kScheme[] = "https://";
  if (url.compare(0, sizeof(kScheme) - 1, kScheme) != 0) return false;
  const size_t hostStart = sizeof(kScheme) - 1;
  size_t hostEnd = url.find_first_of("/?#", hostStart);
  if (hostEnd == std::string::npos) hostEnd = url.size();
  std::string host = lower(url.substr(hostStart, hostEnd - hostStart));
  if (host.find('@') != std::string::npos) return false;  // user@host tricks
  const size_t colon = host.find(':');
  if (colon != std::string::npos) {
    if (host.substr(colon + 1) != "443") return false;
    host.erase(colon);
  }
  const std::string path = url.substr(hostEnd);
  const std::string repo = lower(kUpdateRepo);
  const std::string lowerPath = lower(path);

  if (host == "api.github.com") return lowerPath.rfind("/repos/" + repo + "/releases", 0) == 0;
  if (host == "github.com") return lowerPath.rfind("/" + repo + "/releases/", 0) == 0;
  return host == "release-assets.githubusercontent.com" ||
         host == "objects.githubusercontent.com";
}

// ---- Schedule

CheckOutcome classifyCheckResponse(int httpCode, const std::string &remaining) {
  switch (httpCode) {
    case 200:
      return CheckOutcome::Ok;
    case 304:
      return CheckOutcome::NotModified;
    case 404:
      return CheckOutcome::NoRelease;
    case 429:
      return CheckOutcome::RateLimited;
    case 403:
      return remaining == "0" ? CheckOutcome::RateLimited : CheckOutcome::Failed;
    default:
      return CheckOutcome::Failed;
  }
}

uint32_t rateLimitWaitS(const std::string &retryAfter, const std::string &resetEpoch,
                        std::time_t nowEpoch) {
  auto number = [](const std::string &s, unsigned long long &out) {
    if (s.empty() || s.size() > 12) return false;
    for (char c : s) {
      if (c < '0' || c > '9') return false;
    }
    out = std::strtoull(s.c_str(), nullptr, 10);
    return true;
  };
  unsigned long long v = 0;
  if (number(retryAfter, v) && v > 0) return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
  if (number(resetEpoch, v) && nowEpoch > 0 && v > static_cast<unsigned long long>(nowEpoch)) {
    const unsigned long long d = v - static_cast<unsigned long long>(nowEpoch);
    return d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(d);
  }
  return 0;
}

uint32_t nextCheckDelayMs(CheckOutcome o, uint32_t waitS) {
  switch (o) {
    case CheckOutcome::Ok:
    case CheckOutcome::NotModified:
    case CheckOutcome::NoRelease:
      return kUpdateCheckIntervalMs;
    case CheckOutcome::RateLimited: {
      const uint32_t maxS = kUpdateCheckIntervalMs / 1000;
      const uint32_t minS = kUpdateRetryAfterFailureMs / 1000;
      uint32_t s = waitS < minS ? minS : waitS;
      if (s > maxS) s = maxS;
      return s * 1000u;
    }
    case CheckOutcome::Failed:
    default:
      return kUpdateRetryAfterFailureMs;
  }
}

bool canStartNetworkTask(const CheckGate &g) {
  return g.enabled && g.wifi && g.timeSynced && !g.otaBusy &&
         g.msUntilNextPoll >= kUpdateMinPollGapMs && g.maxFreeBlock >= kUpdateMinFreeBlock;
}

// ---- Status

WebStr updateStatusMessage(UpdateStatus s) {
  switch (s) {
    case UpdateStatus::UpToDate:
      return WebStr::UpdStatusUpToDate;
    case UpdateStatus::Available:
      return WebStr::UpdStatusAvailable;
    case UpdateStatus::BackToRelease:
      return WebStr::UpdStatusBackToRelease;
    case UpdateStatus::DevBuild:
      return WebStr::UpdStatusDevBuild;
    case UpdateStatus::NoRelease:
      return WebStr::UpdStatusNoRelease;
    case UpdateStatus::NoAsset:
      return WebStr::UpdStatusNoAsset;
    case UpdateStatus::NoChecksum:
      return WebStr::UpdStatusNoChecksum;
    case UpdateStatus::CheckFailed:
      return WebStr::UpdStatusCheckFailed;
    case UpdateStatus::RateLimited:
      return WebStr::UpdStatusRateLimited;
    case UpdateStatus::Disabled:
      return WebStr::UpdStatusDisabled;
    case UpdateStatus::NeverChecked:
    default:
      return WebStr::UpdStatusNeverChecked;
  }
}

UpdateStatus statusForRelease(UpdateOffer offer, const ReleaseInfo &r) {
  switch (offer) {
    case UpdateOffer::UpToDate:
      return UpdateStatus::UpToDate;
    case UpdateOffer::BadLatest:
      return UpdateStatus::NoRelease;
    default:
      break;
  }
  if (r.assetUrl.empty()) return UpdateStatus::NoAsset;
  if (r.sha256.empty() && r.sumsUrl.empty()) return UpdateStatus::NoChecksum;
  if (offer == UpdateOffer::BackToRelease) return UpdateStatus::BackToRelease;
  if (offer == UpdateOffer::DevBuild) return UpdateStatus::DevBuild;
  return UpdateStatus::Available;
}

bool statusAllowsInstall(UpdateStatus s) {
  return s == UpdateStatus::Available || s == UpdateStatus::BackToRelease ||
         s == UpdateStatus::DevBuild;
}

bool preReleaseWorthShowing(const ReleaseInfo &pre, const ReleaseInfo &latest) {
  if (pre.tag.empty() || pre.assetUrl.empty()) return false;
  if (latest.tag.empty()) return true;
  return compareFwVersions(parseFwVersion(pre.tag), parseFwVersion(latest.tag)) > 0;
}

// ---- The display prompt

bool shouldShowUpdatePrompt(const PromptGate &g) {
  if (g.status != UpdateStatus::Available || g.latestTag.empty()) return false;
  if (g.installedIsDev || g.alreadyPromptedThisBoot || !g.homeConfigured || g.touchActive ||
      g.screenOff) {
    return false;
  }
  if (!g.skippedTag.empty() &&
      compareFwVersions(parseFwVersion(g.latestTag), parseFwVersion(g.skippedTag)) <= 0) {
    return false;
  }
  // Screen 1 ("NOW") while an aircraft is about to pass overhead or just did:
  // wait — same idea as shouldDeferAutoSwitch(), over the whole approach.
  if (g.currentScreen == 1 && g.cpaFound && g.tCpaSeconds > -5 && g.tCpaSeconds < 60) {
    return false;
  }
  return true;
}

bool acceptPromptTap(bool releasedSinceShown, uint32_t msSinceShown) {
  return releasedSinceShown && msSinceShown >= kUpdatePromptArmMs;
}

UpdateState updateDemoState(bool withPrerelease) {
  UpdateState s;
  s.status = UpdateStatus::Available;
  s.installed = "v1.2.0";
  s.latest.tag = "v1.3.0";
  s.latest.htmlUrl = "https://github.com/" + std::string(kUpdateRepo) + "/releases/tag/v1.3.0";
  s.latest.publishedAt = "2026-10-20T18:02:11Z";
  s.latest.assetUrl = "https://github.com/" + std::string(kUpdateRepo) +
                      "/releases/download/v1.3.0/cyd-sky-tracker-v1.3.0.bin";
  s.latest.assetSize = 1372160;
  s.latest.sha256 = std::string(64, 'a');
  s.checkedAt = 1792833060;  // 24.10.2026 09:11 (local time, as timeSyncNowLocal())
  if (withPrerelease) {
    s.latestPre.tag = "v1.4.0-rc.1";
    s.latestPre.prerelease = true;
    s.latestPre.htmlUrl =
        "https://github.com/" + std::string(kUpdateRepo) + "/releases/tag/v1.4.0-rc.1";
    s.latestPre.publishedAt = "2026-10-22T20:15:00Z";
    s.latestPre.assetUrl = "https://github.com/" + std::string(kUpdateRepo) +
                           "/releases/download/v1.4.0-rc.1/cyd-sky-tracker-v1.4.0-rc.1.bin";
    s.latestPre.assetSize = 1380352;
    s.latestPre.sha256 = std::string(64, 'b');
  }
  return s;
}

#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "github_ca.h"
#include "net_config.h"
#include "time_sync.h"
#include "version.h"

namespace {

UpdateState state;
Config cfg;
bool begun = false;
uint32_t nextCheckAtMs = 0;
bool manualEver = false;
uint32_t lastManualMs = 0;
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
  static uint8_t buf[4096];  // static: not on loopTask's 8 KB stack
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
  if (static_cast<int32_t>(millis() - nextCheckAtMs) < 0) return;
  if (canStartNetworkTask(gate)) runCheck(/*useEtag=*/true);
}

bool releaseCheckNow(const CheckGate &gate) {
  if (!begun) return false;
  if (manualEver && millis() - lastManualMs < kUpdateManualMinGapMs) return false;
  CheckGate g = gate;
  g.enabled = true;
  g.msUntilNextPoll = UINT32_MAX;
  if (!canStartNetworkTask(g)) return false;
  manualEver = true;
  lastManualMs = millis();
  runCheck(/*useEtag=*/true);
  return true;
}

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

#endif  // ARDUINO
