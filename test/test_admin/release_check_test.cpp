// release_check: GitHub release check: versions, JSON, URL allow-list. CLAUDE.md "New versions from GitHub", spec/02.
#include <unity.h>

#include <string>

#include "github_fixtures.h"
#include "release_check.h"
#include "test_support.h"

namespace {

const std::string kRepoUrl = "https://github.com/alasota/CYD-Flight-Tracker";
const std::string kApiUrl = "https://api.github.com/repos/alasota/CYD-Flight-Tracker";

int cmp(const char *a, const char *b) {
  const int c = compareFwVersions(parseFwVersion(a), parseFwVersion(b));
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

ReleaseInfo releaseWith(const char *tag) {
  ReleaseInfo r;
  r.tag = tag;
  r.assetUrl = kRepoUrl + "/releases/download/" + tag + "/cyd-sky-tracker-" + tag + ".bin";
  r.sha256 = std::string(64, 'a');
  return r;
}

PromptGate availableGate() {
  PromptGate g;
  g.status = UpdateStatus::Available;
  g.latestTag = "v1.4.0";
  g.homeConfigured = true;
  return g;
}

// ---- parseFwVersion

static void test_parse_plain_release(void) {
  const FwVersion v = parseFwVersion("v1.3.0");
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_UINT16(1, v.major);
  TEST_ASSERT_EQUAL_UINT16(3, v.minor);
  TEST_ASSERT_EQUAL_UINT16(0, v.patch);
  ASSERT_ENUM_EQ(PreKind::None, v.pre);
  TEST_ASSERT_EQUAL_UINT16(0, v.commitsAhead);
  TEST_ASSERT_FALSE(v.dirty);
  TEST_ASSERT_FALSE(isDevelopmentBuild(v));
}

static void test_parse_git_describe_forms(void) {
  FwVersion v = parseFwVersion("v1.3.0-3-g1a2b3c4");
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_EQUAL_UINT16(3, v.commitsAhead);
  TEST_ASSERT_FALSE(v.dirty);
  TEST_ASSERT_TRUE(isDevelopmentBuild(v));

  v = parseFwVersion("v1.3.0-3-g1a2b3c4-dirty");
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_TRUE(v.dirty);

  v = parseFwVersion("v1.3.0-dirty");
  TEST_ASSERT_TRUE(v.valid);
  TEST_ASSERT_TRUE(v.dirty);
  TEST_ASSERT_TRUE(isDevelopmentBuild(v));
}

static void test_parse_rejects_what_is_not_a_version(void) {
  for (const char *s : {"1a2b3c4", "1a2b3c4-dirty", "dev", "", "v1.3", "v1.3.0.1", "1.3.0",
                        "v65536.0.0", "v1.3.0-", "v1.3.0 ", "v1.3.0-3-g", "v1.3.0-x"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseFwVersion(s).valid, s);
    TEST_ASSERT_TRUE_MESSAGE(isDevelopmentBuild(parseFwVersion(s)), s);
  }
  // Leading zeros are accepted (one decision, pinned here).
  TEST_ASSERT_TRUE(parseFwVersion("v01.2.3").valid);
}

static void test_parse_prerelease_suffixes(void) {
  FwVersion v = parseFwVersion("v1.4.0-rc.2");
  TEST_ASSERT_TRUE(v.valid);
  ASSERT_ENUM_EQ(PreKind::Rc, v.pre);
  TEST_ASSERT_EQUAL_UINT16(2, v.preNumber);
  TEST_ASSERT_TRUE(isDevelopmentBuild(v));
  ASSERT_ENUM_EQ(PreKind::Beta, parseFwVersion("v1.4.0-beta.1").pre);
  ASSERT_ENUM_EQ(PreKind::Dev, parseFwVersion("v1.4.0-dev.7").pre);

  v = parseFwVersion("v1.4.0-rc.1-2-gabc1234");
  TEST_ASSERT_TRUE(v.valid);
  ASSERT_ENUM_EQ(PreKind::Rc, v.pre);
  TEST_ASSERT_EQUAL_UINT16(1, v.preNumber);
  TEST_ASSERT_EQUAL_UINT16(2, v.commitsAhead);

  for (const char *s : {"v1.4.0-rc", "v1.4.0-alpha.1", "v1.4.0-rc.1.2", "v1.4.0-rc.x"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseFwVersion(s).valid, s);
  }
}

static void test_release_tag_format(void) {
  TEST_ASSERT_TRUE(isReleaseTagFormat("v1.4.0"));
  TEST_ASSERT_TRUE(isReleaseTagFormat("v1.4.0-rc.1"));
  TEST_ASSERT_FALSE(isReleaseTagFormat("v1.4.0-3-gabc1234"));
  TEST_ASSERT_FALSE(isReleaseTagFormat("v1.4.0-dirty"));
  TEST_ASSERT_FALSE(isReleaseTagFormat("nightly"));
}

// ---- compareFwVersions / decideOffer

static void test_compare_is_numeric_and_orders_prereleases(void) {
  TEST_ASSERT_EQUAL_INT(-1, cmp("v1.3.9", "v1.10.0"));
  TEST_ASSERT_EQUAL_INT(1, cmp("v2.0.0", "v1.99.99"));
  TEST_ASSERT_EQUAL_INT(0, cmp("v1.4.0", "v1.4.0-3-gabc1234"));  // commits don't count
  const char *order[] = {"v1.4.0-dev.1", "v1.4.0-beta.1", "v1.4.0-rc.1", "v1.4.0-rc.2",
                         "v1.4.0-rc.9",  "v1.4.0-rc.10",  "v1.4.0",      "v1.4.1-rc.1"};
  for (int i = 0; i + 1 < 8; ++i) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, cmp(order[i], order[i + 1]), order[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, cmp(order[i + 1], order[i]), order[i + 1]);
  }
}

static void test_decide_offer(void) {
  ASSERT_ENUM_EQ(UpdateOffer::Newer, decideOffer("v1.3.0", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::Newer, decideOffer("v1.3.9", "v1.10.0"));
  ASSERT_ENUM_EQ(UpdateOffer::UpToDate, decideOffer("v1.4.0", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::UpToDate, decideOffer("v1.4.0-2-gabc1234", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::Newer, decideOffer("v1.4.0-2-gabc1234", "v1.4.1"));
  ASSERT_ENUM_EQ(UpdateOffer::UpToDate, decideOffer("v1.5.0", "v1.4.0"));  // never older
  ASSERT_ENUM_EQ(UpdateOffer::DevBuild, decideOffer("abc1234-dirty", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::DevBuild, decideOffer("dev", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::DevBuild, decideOffer("v1.3.0-dirty", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::BadLatest, decideOffer("v1.3.0", "nightly"));
  ASSERT_ENUM_EQ(UpdateOffer::BackToRelease, decideOffer("v1.5.0-rc.1", "v1.4.0"));
  ASSERT_ENUM_EQ(UpdateOffer::Newer, decideOffer("v1.4.0-rc.2", "v1.4.0"));
}

// ---- Releases

static void test_asset_name(void) {
  TEST_ASSERT_EQUAL_STRING("cyd-sky-tracker-v1.4.0.bin", releaseAssetName("v1.4.0", "cyd-r").c_str());
  TEST_ASSERT_EQUAL_STRING("cyd-sky-tracker-v1.4.0.bin", releaseAssetName("v1.4.0", "").c_str());
  TEST_ASSERT_EQUAL_STRING("cyd-sky-tracker-cyd-c-v1.4.0.bin",
                           releaseAssetName("v1.4.0", "cyd-c").c_str());
}

static void test_parse_real_latest_release(void) {
  ReleaseInfo r;
  TEST_ASSERT_TRUE(parseLatestRelease(kLatestJson, "cyd-r", r));
  TEST_ASSERT_EQUAL_STRING("v1.1.0", r.tag.c_str());
  TEST_ASSERT_FALSE(r.prerelease);
  TEST_ASSERT_EQUAL_STRING((kRepoUrl + "/releases/tag/v1.1.0").c_str(), r.htmlUrl.c_str());
  TEST_ASSERT_EQUAL_STRING("2026-10-24T18:10:03Z", r.publishedAt.c_str());
  // The app image, not -full.bin / bootloader.bin.
  TEST_ASSERT_EQUAL_STRING(
      (kRepoUrl + "/releases/download/v1.1.0/cyd-sky-tracker-v1.1.0.bin").c_str(),
      r.assetUrl.c_str());
  TEST_ASSERT_EQUAL_UINT32(1360144u, static_cast<uint32_t>(r.assetSize));
  TEST_ASSERT_EQUAL_STRING("f7d1a9f370e08a4f4b8cbd200205fe6da2a4c84796fc68ba33a26681e0b2673f",
                           r.sha256.c_str());
  TEST_ASSERT_EQUAL_STRING((kRepoUrl + "/releases/download/v1.1.0/SHA256SUMS.txt").c_str(),
                           r.sumsUrl.c_str());
}

static void test_parse_latest_for_another_board_finds_no_file(void) {
  ReleaseInfo r;
  TEST_ASSERT_TRUE(parseLatestRelease(kLatestJson, "cyd-c", r));
  TEST_ASSERT_TRUE(r.assetUrl.empty());
  ASSERT_ENUM_EQ(UpdateStatus::NoAsset, statusForRelease(UpdateOffer::Newer, r));
}

static void test_parse_latest_rejects_junk_drafts_and_prereleases(void) {
  ReleaseInfo r;
  TEST_ASSERT_FALSE(parseLatestRelease("", "cyd-r", r));
  TEST_ASSERT_FALSE(parseLatestRelease("{\"tag_name\":\"v1.2.0\"", "cyd-r", r));  // cut short
  TEST_ASSERT_FALSE(parseLatestRelease("[]", "cyd-r", r));
  TEST_ASSERT_FALSE(parseLatestRelease("{\"tag_name\":\"v1.2.0\",\"draft\":true}", "cyd-r", r));
  TEST_ASSERT_FALSE(
      parseLatestRelease("{\"tag_name\":\"v1.2.0-rc.1\",\"prerelease\":true}", "cyd-r", r));
  TEST_ASSERT_FALSE(parseLatestRelease("{\"name\":\"no tag\"}", "cyd-r", r));
  // A tag outside the format (could carry markup) is dropped, not shown.
  TEST_ASSERT_FALSE(parseLatestRelease("{\"tag_name\":\"<b>v1</b>\"}", "cyd-r", r));
}

static void test_parse_drops_urls_outside_the_allow_list(void) {
  const char *json =
      "{\"tag_name\":\"v1.2.0\",\"html_url\":\"https://evil.example/x\","
      "\"published_at\":\"2026-10-20\\\"><script>\","
      "\"assets\":[{\"name\":\"cyd-sky-tracker-v1.2.0.bin\",\"size\":1300000,"
      "\"browser_download_url\":\"https://evil.example/cyd-sky-tracker-v1.2.0.bin\"}]}";
  ReleaseInfo r;
  TEST_ASSERT_TRUE(parseLatestRelease(json, "cyd-r", r));
  TEST_ASSERT_TRUE(r.htmlUrl.empty());
  TEST_ASSERT_TRUE(r.publishedAt.empty());
  TEST_ASSERT_TRUE(r.assetUrl.empty());
}

static void test_missing_digest_falls_back_to_the_sums_file(void) {
  const std::string json =
      "{\"tag_name\":\"v1.2.0\",\"assets\":["
      "{\"name\":\"cyd-sky-tracker-v1.2.0.bin\",\"size\":1300000,\"browser_download_url\":\"" +
      kRepoUrl + "/releases/download/v1.2.0/cyd-sky-tracker-v1.2.0.bin\"},"
      "{\"name\":\"SHA256SUMS.txt\",\"size\":433,\"browser_download_url\":\"" + kRepoUrl +
      "/releases/download/v1.2.0/SHA256SUMS.txt\"}]}";
  ReleaseInfo r;
  TEST_ASSERT_TRUE(parseLatestRelease(json, "cyd-r", r));
  TEST_ASSERT_TRUE(r.sha256.empty());
  TEST_ASSERT_FALSE(r.sumsUrl.empty());
  ASSERT_ENUM_EQ(UpdateStatus::Available, statusForRelease(UpdateOffer::Newer, r));
  r.sumsUrl.clear();
  ASSERT_ENUM_EQ(UpdateStatus::NoChecksum, statusForRelease(UpdateOffer::Newer, r));
}

static void test_pick_latest_prerelease(void) {
  ReleaseInfo r;
  TEST_ASSERT_TRUE(pickLatestPrerelease(std::string(kListMixedJson), "cyd-r", r));
  // v1.3.0-rc.1 has no file for the board and v1.4.0-rc.1 is a draft; of
  // the rest, rc.10 beats rc.9 and beta.2.
  TEST_ASSERT_EQUAL_STRING("v1.2.0-rc.10", r.tag.c_str());
  TEST_ASSERT_TRUE(r.prerelease);
  TEST_ASSERT_EQUAL_STRING(
      (kRepoUrl + "/releases/download/v1.2.0-rc.10/cyd-sky-tracker-v1.2.0-rc.10.bin").c_str(),
      r.assetUrl.c_str());
  TEST_ASSERT_EQUAL_INT(64, static_cast<int>(r.sha256.size()));

  TEST_ASSERT_FALSE(pickLatestPrerelease(std::string(kListReleasesOnlyJson), "cyd-r", r));
  TEST_ASSERT_TRUE(r.tag.empty());
  TEST_ASSERT_FALSE(pickLatestPrerelease(std::string("not json"), "cyd-r", r));
}

static void test_prerelease_worth_showing(void) {
  TEST_ASSERT_TRUE(preReleaseWorthShowing(releaseWith("v1.5.0-rc.1"), releaseWith("v1.4.0")));
  TEST_ASSERT_FALSE(preReleaseWorthShowing(releaseWith("v1.4.0-rc.3"), releaseWith("v1.4.0")));
  TEST_ASSERT_TRUE(preReleaseWorthShowing(releaseWith("v1.0.0-rc.1"), ReleaseInfo()));
  ReleaseInfo noFile = releaseWith("v1.5.0-rc.1");
  noFile.assetUrl.clear();
  TEST_ASSERT_FALSE(preReleaseWorthShowing(noFile, releaseWith("v1.4.0")));
  TEST_ASSERT_FALSE(preReleaseWorthShowing(ReleaseInfo(), releaseWith("v1.4.0")));
}

static void test_asset_digest(void) {
  const std::string hex(64, 'a');
  TEST_ASSERT_EQUAL_STRING(hex.c_str(), parseAssetDigest("sha256:" + hex).c_str());
  TEST_ASSERT_EQUAL_STRING(hex.c_str(), parseAssetDigest("sha256:" + std::string(64, 'A')).c_str());
  TEST_ASSERT_TRUE(parseAssetDigest("sha512:" + hex).empty());
  TEST_ASSERT_TRUE(parseAssetDigest("sha256:" + hex.substr(1)).empty());
  TEST_ASSERT_TRUE(parseAssetDigest("sha256:" + std::string(63, 'a') + "g").empty());
  TEST_ASSERT_TRUE(parseAssetDigest("").empty());
}

static void test_sums_file(void) {
  const std::string a(64, 'a'), b(64, 'b'), c(64, 'c');
  const std::string sums = b + "  cyd-sky-tracker-v1.4.0-full.bin\r\n" + a +
                           "  cyd-sky-tracker-v1.4.0.bin\r\n" + c + " *bootloader.bin\n";
  TEST_ASSERT_EQUAL_STRING(a.c_str(), findSha256InSums(sums, "cyd-sky-tracker-v1.4.0.bin").c_str());
  TEST_ASSERT_EQUAL_STRING(b.c_str(),
                           findSha256InSums(sums, "cyd-sky-tracker-v1.4.0-full.bin").c_str());
  TEST_ASSERT_EQUAL_STRING(c.c_str(), findSha256InSums(sums, "bootloader.bin").c_str());
  TEST_ASSERT_TRUE(findSha256InSums(sums, "cyd-sky-tracker-v1.4.bin").empty());
  TEST_ASSERT_TRUE(findSha256InSums("", "x").empty());
}

static void test_iso_date(void) {
  TEST_ASSERT_EQUAL_STRING("20.10.2026", formatIsoDate("2026-10-20T18:02:11Z").c_str());
  TEST_ASSERT_EQUAL_STRING("01.02.2027", formatIsoDate("2027-02-01").c_str());
  TEST_ASSERT_TRUE(formatIsoDate("2026/10/20").empty());
  TEST_ASSERT_TRUE(formatIsoDate("2026-1-20").empty());
  TEST_ASSERT_TRUE(formatIsoDate("").empty());
}

// ---- Allowed URLs

static void test_allowed_urls(void) {
  for (const std::string &u :
       {kApiUrl + "/releases/latest", kApiUrl + "/releases?per_page=10",
        kRepoUrl + "/releases/download/v1.1.0/cyd-sky-tracker-v1.1.0.bin",
        kRepoUrl + "/releases/tag/v1.1.0",
        std::string("https://release-assets.githubusercontent.com/github-production-release-asset/1/x?sig=y"),
        std::string("https://objects.githubusercontent.com/github-production-release-asset-2e65be/1"),
        std::string("https://API.GitHub.com/repos/alasota/cyd-flight-tracker/releases/latest"),
        std::string("https://github.com:443/alasota/CYD-Flight-Tracker/releases/tag/v1.1.0")}) {
    TEST_ASSERT_TRUE_MESSAGE(isAllowedUpdateUrl(u), u.c_str());
  }
}

static void test_refused_urls(void) {
  for (const std::string &u :
       {std::string("http://api.github.com/repos/alasota/CYD-Flight-Tracker/releases/latest"),
        std::string("https://api.github.com.evil.com/repos/alasota/CYD-Flight-Tracker/releases"),
        std::string("https://evil.com/api.github.com/repos/alasota/CYD-Flight-Tracker/releases"),
        std::string("https://github.com/someone/CYD-Flight-Tracker/releases/download/v1/x.bin"),
        std::string("https://github.com/alasota/CYD-Flight-Tracker/archive/main.zip"),
        std::string("https://api.github.com/repos/alasota/other/releases/latest"),
        std::string("https://user@evil.com@github.com/alasota/CYD-Flight-Tracker/releases/x"),
        std::string("https://github.com:8443/alasota/CYD-Flight-Tracker/releases/tag/v1"),
        std::string("https://xrelease-assets.githubusercontent.com/x"),
        std::string("ftp://github.com/alasota/CYD-Flight-Tracker/releases/"), std::string("")}) {
    TEST_ASSERT_FALSE_MESSAGE(isAllowedUpdateUrl(u), u.c_str());
  }
}

// ---- Schedule

static void test_classify_check_response(void) {
  ASSERT_ENUM_EQ(CheckOutcome::Ok, classifyCheckResponse(200, "59"));
  ASSERT_ENUM_EQ(CheckOutcome::NotModified, classifyCheckResponse(304, "59"));
  ASSERT_ENUM_EQ(CheckOutcome::NoRelease, classifyCheckResponse(404, ""));
  ASSERT_ENUM_EQ(CheckOutcome::RateLimited, classifyCheckResponse(429, ""));
  ASSERT_ENUM_EQ(CheckOutcome::RateLimited, classifyCheckResponse(403, "0"));
  ASSERT_ENUM_EQ(CheckOutcome::Failed, classifyCheckResponse(403, "12"));  // e.g. no User-Agent
  ASSERT_ENUM_EQ(CheckOutcome::Failed, classifyCheckResponse(500, ""));
  ASSERT_ENUM_EQ(CheckOutcome::Failed, classifyCheckResponse(-1, ""));
}

static void test_rate_limit_wait(void) {
  TEST_ASSERT_EQUAL_UINT32(120u, rateLimitWaitS("120", "", 1000));
  TEST_ASSERT_EQUAL_UINT32(600u, rateLimitWaitS("", "1600", 1000));
  TEST_ASSERT_EQUAL_UINT32(30u, rateLimitWaitS("30", "1600", 1000));  // Retry-After wins
  TEST_ASSERT_EQUAL_UINT32(0u, rateLimitWaitS("", "900", 1000));      // already past
  TEST_ASSERT_EQUAL_UINT32(0u, rateLimitWaitS("soon", "x", 1000));
  TEST_ASSERT_EQUAL_UINT32(0u, rateLimitWaitS("", "1600", 0));        // clock not set
}

static void test_next_check_delay(void) {
  TEST_ASSERT_EQUAL_UINT32(kUpdateCheckIntervalMs, nextCheckDelayMs(CheckOutcome::Ok, 0));
  TEST_ASSERT_EQUAL_UINT32(kUpdateCheckIntervalMs, nextCheckDelayMs(CheckOutcome::NotModified, 0));
  TEST_ASSERT_EQUAL_UINT32(kUpdateCheckIntervalMs, nextCheckDelayMs(CheckOutcome::NoRelease, 0));
  TEST_ASSERT_EQUAL_UINT32(kUpdateRetryAfterFailureMs, nextCheckDelayMs(CheckOutcome::Failed, 0));
  TEST_ASSERT_EQUAL_UINT32(3600000u, nextCheckDelayMs(CheckOutcome::RateLimited, 10));
  TEST_ASSERT_EQUAL_UINT32(7200000u, nextCheckDelayMs(CheckOutcome::RateLimited, 7200));
  TEST_ASSERT_EQUAL_UINT32(kUpdateCheckIntervalMs,
                           nextCheckDelayMs(CheckOutcome::RateLimited, 10 * 86400));
}

static void test_network_gate(void) {
  CheckGate g;
  g.enabled = true;
  g.wifi = true;
  g.timeSynced = true;
  g.otaBusy = false;
  g.msUntilNextPoll = 5000;
  g.maxFreeBlock = 50 * 1024;
  TEST_ASSERT_TRUE(canStartNetworkTask(g));

  CheckGate x = g;
  x.msUntilNextPoll = 4999;
  TEST_ASSERT_FALSE(canStartNetworkTask(x));
  x = g;
  x.maxFreeBlock = 50 * 1024 - 1;
  TEST_ASSERT_FALSE(canStartNetworkTask(x));
  x = g;
  x.enabled = false;
  TEST_ASSERT_FALSE(canStartNetworkTask(x));
  x = g;
  x.wifi = false;
  TEST_ASSERT_FALSE(canStartNetworkTask(x));
  x = g;
  x.timeSynced = false;
  TEST_ASSERT_FALSE(canStartNetworkTask(x));
  x = g;
  x.otaBusy = true;
  TEST_ASSERT_FALSE(canStartNetworkTask(x));
}

// ---- Status

static void test_every_status_has_a_message(void) {
  for (int i = 0; i < kUpdateStatusCount; ++i) {
    const WebStr id = updateStatusMessage(static_cast<UpdateStatus>(i));
    TEST_ASSERT_TRUE(static_cast<int>(id) < kWebStrCount);
    TEST_ASSERT_TRUE(webTr(id, Language::PL)[0] != '\0');
    TEST_ASSERT_TRUE(webTr(id, Language::EN)[0] != '\0');
    if (i > 0) {
      TEST_ASSERT_TRUE(id != updateStatusMessage(UpdateStatus::NeverChecked));
    }
  }
}

static void test_status_for_release(void) {
  const ReleaseInfo r = releaseWith("v1.4.0");
  ASSERT_ENUM_EQ(UpdateStatus::Available, statusForRelease(UpdateOffer::Newer, r));
  ASSERT_ENUM_EQ(UpdateStatus::UpToDate, statusForRelease(UpdateOffer::UpToDate, r));
  ASSERT_ENUM_EQ(UpdateStatus::BackToRelease, statusForRelease(UpdateOffer::BackToRelease, r));
  ASSERT_ENUM_EQ(UpdateStatus::DevBuild, statusForRelease(UpdateOffer::DevBuild, r));
  ASSERT_ENUM_EQ(UpdateStatus::NoRelease, statusForRelease(UpdateOffer::BadLatest, r));
  TEST_ASSERT_TRUE(statusAllowsInstall(UpdateStatus::Available));
  TEST_ASSERT_TRUE(statusAllowsInstall(UpdateStatus::BackToRelease));
  TEST_ASSERT_TRUE(statusAllowsInstall(UpdateStatus::DevBuild));
  TEST_ASSERT_FALSE(statusAllowsInstall(UpdateStatus::UpToDate));
  TEST_ASSERT_FALSE(statusAllowsInstall(UpdateStatus::NoChecksum));
  TEST_ASSERT_FALSE(statusAllowsInstall(UpdateStatus::CheckFailed));
}

// ---- The display prompt

static void test_prompt_only_for_an_available_release(void) {
  TEST_ASSERT_TRUE(shouldShowUpdatePrompt(availableGate()));
  for (UpdateStatus s : {UpdateStatus::UpToDate, UpdateStatus::DevBuild, UpdateStatus::BackToRelease,
                         UpdateStatus::NoChecksum, UpdateStatus::CheckFailed}) {
    PromptGate g = availableGate();
    g.status = s;
    TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
  }
}

static void test_prompt_not_on_a_dark_screen(void) {
  PromptGate g = availableGate();
  g.screenOff = true;
  TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
}

static void test_prompt_skipped_versions(void) {
  PromptGate g = availableGate();
  g.skippedTag = "v1.4.0";
  TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
  g.skippedTag = "v1.3.5";
  TEST_ASSERT_TRUE(shouldShowUpdatePrompt(g));  // a newer one than the skipped is asked again
  g.latestTag = "v1.4.1";
  g.skippedTag = "v1.4.0";
  TEST_ASSERT_TRUE(shouldShowUpdatePrompt(g));
}

static void test_prompt_never_for_development_builds_or_twice(void) {
  PromptGate g = availableGate();
  g.installedIsDev = true;
  TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
  g = availableGate();
  g.alreadyPromptedThisBoot = true;
  TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
  g = availableGate();
  g.homeConfigured = false;
  TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
  g = availableGate();
  g.touchActive = true;
  TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
}

static void test_prompt_waits_for_the_overhead_moment(void) {
  PromptGate g = availableGate();
  g.currentScreen = 1;  // NOW
  g.cpaFound = true;
  for (int t : {59, 30, 0, -4}) {
    g.tCpaSeconds = t;
    TEST_ASSERT_FALSE(shouldShowUpdatePrompt(g));
  }
  for (int t : {60, 300, -5, -30}) {
    g.tCpaSeconds = t;
    TEST_ASSERT_TRUE(shouldShowUpdatePrompt(g));
  }
  g.tCpaSeconds = 10;
  g.currentScreen = 0;  // the countdown isn't on screen
  TEST_ASSERT_TRUE(shouldShowUpdatePrompt(g));
  g.currentScreen = 1;
  g.cpaFound = false;
  TEST_ASSERT_TRUE(shouldShowUpdatePrompt(g));
}

static void test_prompt_tap_needs_a_fresh_touch(void) {
  TEST_ASSERT_FALSE(acceptPromptTap(false, 5000));  // finger still down from before
  TEST_ASSERT_FALSE(acceptPromptTap(true, kUpdatePromptArmMs - 1));
  TEST_ASSERT_TRUE(acceptPromptTap(true, kUpdatePromptArmMs));
}

static void test_demo_state_is_consistent(void) {
  const UpdateState s = updateDemoState(true);
  ASSERT_ENUM_EQ(UpdateOffer::Newer, decideOffer(s.installed, s.latest.tag));
  ASSERT_ENUM_EQ(s.status, statusForRelease(UpdateOffer::Newer, s.latest));
  TEST_ASSERT_TRUE(isAllowedUpdateUrl(s.latest.assetUrl));
  TEST_ASSERT_TRUE(isAllowedUpdateUrl(s.latestPre.htmlUrl));
  TEST_ASSERT_TRUE(preReleaseWorthShowing(s.latestPre, s.latest));
  TEST_ASSERT_TRUE(updateDemoState(false).latestPre.tag.empty());
}

}  // namespace

void run_release_check_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_parse_plain_release);
  RUN_TEST(test_parse_git_describe_forms);
  RUN_TEST(test_parse_rejects_what_is_not_a_version);
  RUN_TEST(test_parse_prerelease_suffixes);
  RUN_TEST(test_release_tag_format);
  RUN_TEST(test_compare_is_numeric_and_orders_prereleases);
  RUN_TEST(test_decide_offer);
  RUN_TEST(test_asset_name);
  RUN_TEST(test_parse_real_latest_release);
  RUN_TEST(test_parse_latest_for_another_board_finds_no_file);
  RUN_TEST(test_parse_latest_rejects_junk_drafts_and_prereleases);
  RUN_TEST(test_parse_drops_urls_outside_the_allow_list);
  RUN_TEST(test_missing_digest_falls_back_to_the_sums_file);
  RUN_TEST(test_pick_latest_prerelease);
  RUN_TEST(test_prerelease_worth_showing);
  RUN_TEST(test_asset_digest);
  RUN_TEST(test_sums_file);
  RUN_TEST(test_iso_date);
  RUN_TEST(test_allowed_urls);
  RUN_TEST(test_refused_urls);
  RUN_TEST(test_classify_check_response);
  RUN_TEST(test_rate_limit_wait);
  RUN_TEST(test_next_check_delay);
  RUN_TEST(test_network_gate);
  RUN_TEST(test_every_status_has_a_message);
  RUN_TEST(test_status_for_release);
  RUN_TEST(test_prompt_only_for_an_available_release);
  RUN_TEST(test_prompt_skipped_versions);
  RUN_TEST(test_prompt_not_on_a_dark_screen);
  RUN_TEST(test_prompt_never_for_development_builds_or_twice);
  RUN_TEST(test_prompt_waits_for_the_overhead_moment);
  RUN_TEST(test_prompt_tap_needs_a_fresh_touch);
  RUN_TEST(test_demo_state_is_consistent);
}
