#include "admin/core/firmware/release_check.h"

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

// ---- "Check now"

ManualCheckAsk askManualCheck(ManualCheck &m, uint32_t nowMs) {
  if (manualCheckPending(m, nowMs)) return ManualCheckAsk::AlreadyQueued;
  if (m.ran && nowMs - m.lastRunMs < kUpdateManualMinGapMs) {
    m.queued = false;
    return ManualCheckAsk::TooSoon;
  }
  m.queued = true;
  m.askedMs = nowMs;
  return ManualCheckAsk::Queued;
}

bool takeManualCheck(ManualCheck &m, bool gateOpen, uint32_t nowMs) {
  if (!manualCheckPending(m, nowMs)) {
    m.queued = false;  // expired (or never asked)
    return false;
  }
  if (!gateOpen) return false;
  m.queued = false;
  m.ran = true;
  m.lastRunMs = nowMs;
  return true;
}

bool manualCheckPending(const ManualCheck &m, uint32_t nowMs) {
  return m.queued && nowMs - m.askedMs < kUpdateManualCheckExpiryMs;
}

// ---- Install asked for by hand

// TODO(fix fix-github-install-never-starts): stub from bug-investigator.
InstallStep stepPendingInstall(const CheckGate &g, uint32_t /*askedMs*/, uint32_t /*nowMs*/) {
  CheckGate asked = g;
  asked.enabled = true;
  asked.msUntilNextPoll = UINT32_MAX;
  return canStartNetworkTask(asked) ? InstallStep::Start : InstallStep::Wait;
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
      g.screenOff || g.alertScreenOpen) {
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
