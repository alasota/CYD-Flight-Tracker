// release_check — "is there a newer version on GitHub?" (spec/done/02-github-release-check.md):
// asks GitHub's API for the latest release once a day, says so on the config
// page and once on the display, and after one confirmation downloads
// cyd-sky-tracker-<tag>.bin from the release and installs it through
// ota_update's OtaSession (header, marker, SHA-256, rollback).
//
// Two channels: releases (vX.Y.Z) — shown everywhere, asked about on the
// display; development versions (GitHub pre-releases, vX.Y.Z-rc.N /
// -beta.N / -dev.N) — only on the config page, and only when the admin ticks
// "show development versions". Never installs without being asked.
//
// The logic (versions, picking the file, parsing GitHub's answers, allowed
// hosts, schedule, when to ask) is pure and tested under `pio test -e
// native`; the HTTPS check + download below the ARDUINO guard is the thin
// adapter (WiFiClientSecure with the CA roots of github_ca.h).
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <ctime>
#include <string>

#include "ota_update.h"  // OtaResult
#include "web_i18n.h"    // WebStr

// The repository updates come from — fixed in the firmware (a fork sets
// -DFW_UPDATE_REPO=\"owner/repo\"), never taken from the settings or the
// network: whoever can reach the panel must not be able to point updates
// at their own repository.
#ifndef FW_UPDATE_REPO
#define FW_UPDATE_REPO "alasota/CYD-Flight-Tracker"
#endif
constexpr const char *kUpdateRepo = FW_UPDATE_REPO;

// ---- Versions. Release tags "v<major>.<minor>.<patch>", development ones
// with "-dev.N" / "-beta.N" / "-rc.N" (dev < beta < rc < release, as in
// semver). An installed build may carry git describe's "-<n>-g<hash>" and
// "-dirty" on top.
enum class PreKind : uint8_t { Dev, Beta, Rc, None };  // None = a release, the highest
struct FwVersion {
  bool valid = false;  // major.minor.patch (and a well-formed suffix) could be read
  uint16_t major = 0, minor = 0, patch = 0;
  PreKind pre = PreKind::None;
  uint16_t preNumber = 0;     // "-rc.2" -> 2
  uint16_t commitsAhead = 0;  // "v1.3.0-3-g1a2b3c4" -> 3
  bool dirty = false;         // "...-dirty"
};
FwVersion parseFwVersion(const std::string &s);
// A tag GitHub may hold: valid, no commits ahead, not dirty.
bool isReleaseTagFormat(const std::string &tag);
// <0 / 0 / >0 by (major, minor, patch, pre, preNumber); commitsAhead and
// dirty don't count.
int compareFwVersions(const FwVersion &a, const FwVersion &b);
// Installed build that isn't a plain release: a pre-release suffix,
// commits after the tag, uncommitted changes, or no version at all.
bool isDevelopmentBuild(const FwVersion &installed);

enum class UpdateOffer : uint8_t {
  Newer,          // latest > installed — offer it
  UpToDate,       // latest == installed, or installed is a local build after that tag
  BackToRelease,  // installed is a development version above latest — panel: "go back"
  DevBuild,       // installed can't be compared ("dev", a bare hash, "-dirty")
  BadLatest,      // latest's tag isn't in the release format — ignore that release
};
UpdateOffer decideOffer(const std::string &installed, const std::string &latestTag);

// ---- A release on GitHub
struct ReleaseInfo {
  std::string tag;          // "v1.4.0"
  std::string htmlUrl;      // the release page
  std::string publishedAt;  // ISO 8601
  std::string assetUrl;     // browser_download_url of this board's .bin, "" = none
  size_t assetSize = 0;
  std::string sha256;   // 64 lower-case hex from the asset's digest, "" = none
  std::string sumsUrl;  // SHA256SUMS.txt of the release, "" = none
  bool prerelease = false;
};
// "cyd-sky-tracker-<tag>.bin" for the classic board ("cyd-r"); other boards
// (ROADMAP #3) "cyd-sky-tracker-<board>-<tag>.bin".
std::string releaseAssetName(const std::string &tag, const std::string &board);

// Keeps only the fields above out of GitHub's (long) answers — the release
// notes and the uploader's profile never reach RAM. `list` = the
// /releases array instead of one /releases/latest object.
void buildReleaseFilter(JsonDocument &filter, bool list);
// One release object -> ReleaseInfo. false: a draft, no / malformed tag,
// or a URL outside isAllowedUpdateUrl(). Pre-releases are accepted here
// (`out.prerelease` says which); the callers decide.
bool releaseFromJson(JsonVariantConst release, const std::string &board, ReleaseInfo &out);
// /releases/latest. false: bad JSON, a draft or pre-release, no tag.
bool parseLatestRelease(const std::string &json, const std::string &board, ReleaseInfo &out);
// The highest pre-release (GitHub's flag decides, not the tag's suffix)
// that has a file for `board`, from a /releases list. false: none.
bool pickLatestPrerelease(JsonVariantConst list, const std::string &board, ReleaseInfo &out);
bool pickLatestPrerelease(const std::string &json, const std::string &board, ReleaseInfo &out);

// "sha256:<64 hex>" -> 64 lower-case hex; "" for anything else.
std::string parseAssetDigest(const std::string &digest);
// The hash on the "<hex>  <name>" (or "<hex> *<name>") line of a sha256sum
// file; "" when the name isn't there.
std::string findSha256InSums(const std::string &sums, const std::string &assetName);
// "2026-10-20T18:02:11Z" -> "20.10.2026"; "" when malformed.
std::string formatIsoDate(const std::string &iso);

// ---- Where the device may connect: https only, and
//   api.github.com                         /repos/<repo>/releases...
//   github.com                             /<repo>/releases/...
//   release-assets.githubusercontent.com   any path (signed download URLs)
//   objects.githubusercontent.com          any path (the older download host)
// Checked for the first URL and for every redirect before connecting.
bool isAllowedUpdateUrl(const std::string &url);
constexpr int kMaxUpdateRedirects = 3;

// ---- Schedule (millis(); every comparison wrap-safe)
constexpr uint32_t kUpdateFirstCheckAfterMs = 120000;                  // 2 min after boot
constexpr uint32_t kUpdateCheckIntervalMs = 24UL * 3600UL * 1000UL;    // then daily
constexpr uint32_t kUpdateRetryAfterFailureMs = 3600UL * 1000UL;       // network / TLS: 1 h
constexpr uint32_t kUpdateManualMinGapMs = 60000;  // "check now" at most once a minute
constexpr uint32_t kUpdateMinPollGapMs = 5000;     // keep clear of the next OpenSky poll
constexpr uint32_t kUpdateMinFreeBlock = 50 * 1024;  // one TLS session needs ~40-50 KB

enum class CheckOutcome : uint8_t { Ok, NotModified, NoRelease, RateLimited, Failed };
// HTTP answer of an API call -> outcome. `remaining` = X-RateLimit-Remaining
// ("" when absent): a 403 is the rate limit only when it says "0".
CheckOutcome classifyCheckResponse(int httpCode, const std::string &remaining);
// Seconds GitHub asks to wait: Retry-After, else X-RateLimit-Reset (epoch)
// minus `nowEpoch`; 0 when neither says anything usable.
uint32_t rateLimitWaitS(const std::string &retryAfter, const std::string &resetEpoch,
                        std::time_t nowEpoch);
// When to check next, in ms from now. A rate limit waits what GitHub asked,
// at least the failure retry and at most a day.
uint32_t nextCheckDelayMs(CheckOutcome o, uint32_t rateLimitWaitS);

// May a background check (or the download) start right now?
struct CheckGate {
  bool enabled = false;      // update_check_enabled (a manual check ignores it)
  bool wifi = false;         // connected to a network
  bool timeSynced = false;   // certificates need a real date
  bool otaBusy = false;      // an update is already being written / restarting
  uint32_t msUntilNextPoll = 0;  // until the next OpenSky poll is due
  uint32_t maxFreeBlock = 0;     // largest free heap block
};
bool canStartNetworkTask(const CheckGate &g);

// ---- What the config page and main.cpp see
enum class UpdateStatus : uint8_t {
  NeverChecked,
  UpToDate,
  Available,      // a newer release, ready to install
  BackToRelease,  // a development version is installed; the release is lower
  DevBuild,       // installed build can't be compared — the release can still be installed
  NoRelease,      // the repository has no release yet
  NoAsset,        // the release has no file for this board
  NoChecksum,     // the release has no SHA-256 to check the file against
  CheckFailed,    // network / TLS / unexpected answer
  RateLimited,
  Disabled,       // checking switched off (and never checked by hand)
};
constexpr int kUpdateStatusCount = 11;
WebStr updateStatusMessage(UpdateStatus s);
// Status for a release that was found.
UpdateStatus statusForRelease(UpdateOffer offer, const ReleaseInfo &r);
// Whether a status lets the page offer the release's install button.
bool statusAllowsInstall(UpdateStatus s);

struct UpdateState {
  UpdateStatus status = UpdateStatus::NeverChecked;
  std::string installed;  // firmwareVersion()
  ReleaseInfo latest;     // tag "" until a check found a release
  ReleaseInfo latestPre;  // tag "" unless "show development versions" found one above latest
  std::time_t checkedAt = 0;  // local time of the last answer, 0 = never
  int lastError = 0;          // HTTP status or (negative) TLS error, for the page
  OtaResult lastInstall = OtaResult::NoFile;  // NoFile = no failed install this session
  std::string lastInstallTag;
};

// The development row is worth showing: a pre-release above the latest
// release (or with no release at all).
bool preReleaseWorthShowing(const ReleaseInfo &pre, const ReleaseInfo &latest);

// ---- The question on the display ("NEW VERSION v1.4.0 — INSTALL?")
struct PromptGate {
  UpdateStatus status = UpdateStatus::NeverChecked;
  std::string latestTag;
  std::string skippedTag;          // update_skipped_tag: NO was tapped for it
  bool installedIsDev = false;     // a development build never gets asked
  bool alreadyPromptedThisBoot = false;
  bool homeConfigured = false;     // the setup prompt has the screen
  int currentScreen = 0;
  bool cpaFound = false;           // as for shouldDeferAutoSwitch()
  int tCpaSeconds = 0;
  bool touchActive = false;        // never pop up under a finger
  bool screenOff = false;          // night, backlight off (backlight) — nobody would see it
  bool alertScreenOpen = false;    // the device-alert message is up (spec 08) — not over it
};
bool shouldShowUpdatePrompt(const PromptGate &g);
constexpr uint32_t kUpdatePromptTimeoutMs = 30000;
// A tap on the prompt counts only once the finger that may have been on the
// screen when it appeared has been lifted, and the prompt has been up for
// kUpdatePromptArmMs — so a tap meant for the screen below can't answer it.
constexpr uint32_t kUpdatePromptArmMs = 300;
bool acceptPromptTap(bool releasedSinceShown, uint32_t msSinceShown);

// ---- Demo state for screenshots and tests (docs/screenshots/update_prompt.png,
// web_firmware*.png): installed v1.2.0, v1.3.0 available, optionally
// v1.4.0-rc.1 in the development channel.
UpdateState updateDemoState(bool withPrerelease);

#ifdef ARDUINO

#include "config_portal.h"  // OtaObserver
#include "config_store.h"   // Config

// Takes the settings (call again after they change).
void releaseCheckBegin(const Config &cfg);
// From loop(): starts a due check or an asked-for install when `gate` allows.
// Blocks while it runs (~1-3 s for a check, ~30-60 s for an install, with
// the OTA screen up), like an OpenSky poll.
void releaseCheckLoop(const CheckGate &gate);
// "Check now" (config page): runs at once unless the last check was under
// kUpdateManualMinGapMs ago. Returns whether it ran.
bool releaseCheckNow(const CheckGate &gate);
// Queues an install of `tag` for the next releaseCheckLoop(). false: `tag`
// is neither the latest release nor the shown development version, or an
// install is already queued.
bool releaseInstallRequest(const std::string &tag);
bool releaseInstallPending();
const UpdateState &releaseUpdateState();
void releaseSetOtaObserver(const OtaObserver &observer);

#endif  // ARDUINO
