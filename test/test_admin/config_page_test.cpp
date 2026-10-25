// config_page: config page HTML, tabs, credits readouts, OpenSky login state (spec 06). CLAUDE.md "Code conventions" (config_portal / config_page).
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "config_page.h"
#include "test_support.h"

namespace {

int emptyChunks = 0;

std::string render(const ConfigPageModel &m) {
  std::string html;
  emptyChunks = 0;
  renderConfigPage(m, [&html](const char *chunk) {
    if (*chunk == '\0') ++emptyChunks;
    html += chunk;
  });
  return html;
}

ConfigPageModel baseModel() {
  ConfigPageModel m;
  m.cfg = defaultConfig();
  m.cfg.home_lat = 52.2297f;
  m.cfg.home_lon = 21.0122f;
  return m;
}

// ---- pure helpers -----------------------------------------------------------

static void test_tab_ids_round_trip(void) {
  for (SettingsTab t : {SettingsTab::Location, SettingsTab::OpenSky, SettingsTab::General,
                        SettingsTab::Backup, SettingsTab::Firmware}) {
    ASSERT_ENUM_EQ(t, settingsTabFromArg(settingsTabId(t)));
  }
  ASSERT_ENUM_EQ(SettingsTab::Location, settingsTabFromArg(""));
  ASSERT_ENUM_EQ(SettingsTab::Location, settingsTabFromArg("nonsense"));
}

static void test_daily_credits(void) {
  // 15 s polls -> 5760 requests/day, 1 credit each at radius 2.5.
  TEST_ASSERT_EQUAL_UINT32(5760u, openSkyDailyCredits(2.5f, 15));
  // Radius 5 -> 2 credits per request.
  TEST_ASSERT_EQUAL_UINT32(11520u, openSkyDailyCredits(5.0f, 15));
}

static void test_daily_credits_rounding_and_zero_interval(void) {
  TEST_ASSERT_EQUAL_UINT32(1235u, openSkyDailyCredits(2.5f, 70));  // 86400/70 = 1234.3 -> 1235
  TEST_ASSERT_EQUAL_UINT32(86400u, openSkyDailyCredits(2.5f, 0));  // 0 treated as 1 s
}

static void test_daily_credits_with_a_night_pause(void) {
  // 15 s polls, 23:00-06:30 paused (450 min) -> 5760 - 1800 (spec/03 §5.6).
  TEST_ASSERT_EQUAL_UINT32(3960u, openSkyDailyRequests(15, 450));
  TEST_ASSERT_EQUAL_UINT32(3960u, openSkyDailyCredits(2.5f, 15, 450));
  TEST_ASSERT_EQUAL_UINT32(5760u, openSkyDailyRequests(15, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, openSkyDailyRequests(15, 1440));  // polar night, all paused
  TEST_ASSERT_EQUAL_UINT32(0u, openSkyDailyRequests(15, 5000));
}

static void test_paused_minutes_follow_the_settings(void) {
  Config c = defaultConfig();
  BacklightStatus l;
  c.night_mode = NightMode::Schedule;  // 23:00-06:30 by default
  TEST_ASSERT_EQUAL_UINT32(450u, nightMinutesToday(c, l));
  TEST_ASSERT_EQUAL_UINT32(0u, pausedMinutesPerDay(c, l));  // pause off (default)
  c.night_pause_polling = true;
  TEST_ASSERT_EQUAL_UINT32(450u, pausedMinutesPerDay(c, l));
  c.night_mode = NightMode::Sun;  // sun not known yet
  TEST_ASSERT_EQUAL_UINT32(0u, pausedMinutesPerDay(c, l));
  l.sun_known = true;
  l.sun_local.sunrise_min = 7 * 60 + 17;
  l.sun_local.sunset_min = 17 * 60 + 23;
  TEST_ASSERT_EQUAL_UINT32(834u, pausedMinutesPerDay(c, l));  // 17:23 -> 07:17
  c.night_mode = NightMode::Off;
  TEST_ASSERT_EQUAL_UINT32(0u, pausedMinutesPerDay(c, l));
}

static void test_light_state_text(void) {
  BacklightStatus l;
  TEST_ASSERT_EQUAL_STRING("clock not synced — night mode inactive",
                           lightStateText(l, Language::EN).c_str());
  l.synced = true;
  TEST_ASSERT_EQUAL_STRING("dzień", lightStateText(l, Language::PL).c_str());
  l.night = true;
  TEST_ASSERT_EQUAL_STRING("night", lightStateText(l, Language::EN).c_str());
  l.woken = true;
  TEST_ASSERT_EQUAL_STRING("night, woken by a touch", lightStateText(l, Language::EN).c_str());
  l.woken = false;
  l.screen_off = true;
  l.polling = false;
  TEST_ASSERT_EQUAL_STRING("night, screen off · fetching paused",
                           lightStateText(l, Language::EN).c_str());
}

static void test_display_card(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.cfg.home_configured = true;
  std::string h = render(m);
  // Defaults: automatic brightness, sunset to sunrise, no pause.
  ASSERT_CONTAINS(h, "<option value=\"auto\" selected>automatic (light sensor)");
  ASSERT_CONTAINS(h, "<option value=\"sun\" selected>sunset to sunrise");
  ASSERT_CONTAINS(h, "<div id=\"blauto\">");
  ASSERT_CONTAINS(h, "<div id=\"blfix\" hidden>");
  ASSERT_CONTAINS(h, "<div id=\"ntsch\" hidden>");
  ASSERT_CONTAINS(h, "<div id=\"ntopt\">");
  ASSERT_CONTAINS(h, "name=\"bl_min\" min=\"5\" max=\"100\" value=\"10\"");
  ASSERT_CONTAINS(h, "name=\"night_pct\" min=\"0\" max=\"100\" value=\"10\"");
  ASSERT_CONTAINS(h, "name=\"night_wake_s\" min=\"10\" max=\"600\" value=\"60\"");
  ASSERT_CONTAINS(h, "name=\"night_pause\" value=\"1\">");
  ASSERT_NOT_CONTAINS(h, "name=\"night_pause\" value=\"1\" checked");
  ASSERT_CONTAINS(h, "Sunrise and sunset appear once the clock is synced.");

  m.cfg.brightness_auto = false;
  m.cfg.night_mode = NightMode::Schedule;
  m.cfg.night_from_min = 22 * 60 + 30;
  m.cfg.night_pause_polling = true;
  m.light.synced = true;
  m.light.sun_known = true;
  m.light.sun_local.sunrise_min = 7 * 60 + 17;
  m.light.sun_local.sunset_min = 17 * 60 + 23;
  m.light.raw = 1234;
  m.light.pct = 35;
  h = render(m);
  ASSERT_CONTAINS(h, "<option value=\"fixed\" selected>fixed");
  ASSERT_CONTAINS(h, "<div id=\"blfix\">");
  ASSERT_CONTAINS(h, "<div id=\"ntsch\">");
  ASSERT_CONTAINS(h, "type=\"time\" name=\"night_from\" value=\"22:30\"");
  ASSERT_CONTAINS(h, "name=\"night_pause\" value=\"1\" checked");
  ASSERT_CONTAINS(h, "<b id=\"lr\">1234</b>");
  ASSERT_CONTAINS(h, "<b id=\"lp\">35</b> %");
  ASSERT_CONTAINS(h, "Sun today: sunrise 07:17, sunset 17:23");
  // 22:30-06:30 = 480 min at 15 s = 1920 fewer requests; the OpenSky tab counts them.
  ASSERT_CONTAINS(h, "<b id=\"ntsv\">1920</b> fewer requests tonight");
  ASSERT_CONTAINS(h, "data-night=\"480\">≈ <b>3840</b>");

  m.cfg.night_mode = NightMode::Off;
  m.cfg.home_configured = false;
  h = render(m);
  ASSERT_CONTAINS(h, "<div id=\"ntopt\" hidden>");
  ASSERT_CONTAINS(h, "Set the location to work out sunrise and sunset.");
}

static void test_credentials_need_both_fields(void) {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(hasOpenSkyCredentials(c));
  c.opensky_client_id = "id";
  TEST_ASSERT_FALSE(hasOpenSkyCredentials(c));
  c.opensky_client_secret = "secret";
  TEST_ASSERT_TRUE(hasOpenSkyCredentials(c));
}

static void test_html_escape(void) {
  TEST_ASSERT_EQUAL_STRING("a&amp;b &lt;x&gt; &quot;q&quot;", htmlEscape("a&b <x> \"q\"").c_str());
  TEST_ASSERT_EQUAL_STRING("plain", htmlEscape("plain").c_str());
}

// ---- rendered page ----------------------------------------------------------

static void test_page_is_complete_and_never_emits_empty_chunks(void) {
  ConfigPageModel m = baseModel();
  m.cfg.opensky_client_id = "";  // empty values must not produce empty chunks
  const std::string h = render(m);
  TEST_ASSERT_EQUAL_INT(0, emptyChunks);
  ASSERT_CONTAINS(h, "<!DOCTYPE html>");
  ASSERT_CONTAINS(h, "</html>");
}

static void test_page_has_the_project_favicon(void) {
  const std::string h = render(baseModel());
  ASSERT_CONTAINS(h, "<link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,");
  ASSERT_CONTAINS(h, "fill='%23ff9966'");      // peach plane, '#' escaped
  TEST_ASSERT_TRUE(h.find("<link rel=\"icon\"") < h.find("</head>"));
}

static void test_all_form_fields_present(void) {
  const std::string h = render(baseModel());
  const char *names[] = {"name=\"lat\"", "name=\"lon\"", "name=\"radius\"",
                         "name=\"poll_interval\"", "name=\"client_id\"", "name=\"client_secret\"",
                         "name=\"language\"", "name=\"web_language\"", "name=\"auto_cycle\"",
                         "name=\"auto_cycle_interval\"", "name=\"near_airport_km\"",
                         "name=\"climb_rate_mps\"", "name=\"tab\"", "name=\"creds_file\""};
  for (const char *n : names) ASSERT_CONTAINS(h, n);
  ASSERT_CONTAINS(h, "id=\"lat\" name=\"lat\" inputmode=\"decimal\" autocomplete=\"off\" value=\"52.22970\"");
}

static void test_only_the_active_tab_is_visible(void) {
  ConfigPageModel m = baseModel();
  m.tab = SettingsTab::OpenSky;
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "<section data-tab=\"opensky\">");
  ASSERT_CONTAINS(h, "<section data-tab=\"location\" hidden>");
  ASSERT_CONTAINS(h, "<section data-tab=\"general\" hidden>");
  ASSERT_CONTAINS(h, "data-t=\"opensky\" class=\"on\"");
  ASSERT_CONTAINS(h, "id=\"tab\" value=\"opensky\"");
}

static void test_web_language_switches_the_page_text(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.cfg.language = Language::PL;  // the display language doesn't matter here
  std::string h = render(m);
  ASSERT_CONTAINS(h, "<html lang=\"en\">");
  ASSERT_CONTAINS(h, ">Location</button>");
  ASSERT_CONTAINS(h, "class=\"on\" href=\"/lang?l=en\"");

  m.cfg.web_language = Language::PL;
  h = render(m);
  ASSERT_CONTAINS(h, "<html lang=\"pl\">");
  ASSERT_CONTAINS(h, ">Lokalizacja</button>");
  ASSERT_CONTAINS(h, "class=\"on\" href=\"/lang?l=pl\"");
}

static void test_selects_reflect_both_languages_independently(void) {
  ConfigPageModel m = baseModel();
  m.cfg.language = Language::EN;
  m.cfg.web_language = Language::PL;
  const std::string h = render(m);
  const size_t display = h.find("name=\"language\"");
  const size_t web = h.find("name=\"web_language\"");
  TEST_ASSERT_TRUE(display != std::string::npos && web != std::string::npos);
  TEST_ASSERT_TRUE(h.find("value=\"en\" selected", display) < web);       // display: EN
  ASSERT_CONTAINS(h.substr(web), "value=\"pl\" selected");  // web: PL
}

static void test_user_values_are_escaped(void) {
  ConfigPageModel m = baseModel();
  m.cfg.opensky_client_id = "a\"><script>x</script>";
  const std::string h = render(m);
  ASSERT_NOT_CONTAINS(h, "<script>x</script>");
  ASSERT_CONTAINS(h, "a&quot;&gt;&lt;script&gt;");
}

static void test_message_and_detail(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.hasMessage = true;
  m.message = WebStr::SavedWithInvalid;
  m.isError = true;
  m.messageDetail = "Latitude, <Radius>";
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "<div class=\"msg err\">Saved, but");
  ASSERT_CONTAINS(h, "Latitude, &lt;Radius&gt;");

  m.hasMessage = false;
  ASSERT_NOT_CONTAINS(render(m), "class=\"msg");
}

static void test_status_and_daily_credit_readout(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  std::string h = render(m);  // anonymous, 15 s polls: 5760 > 400
  ASSERT_CONTAINS(h, "Anonymous mode");
  ASSERT_CONTAINS(h, "<b>5760</b> credits a day");
  ASSERT_CONTAINS(h, "limit 400<div class=\"warn\">");

  m.cfg.opensky_client_id = "id";
  m.cfg.opensky_client_secret = "secret";
  m.cfg.poll_interval_s = 30;  // 2880 <= 4000
  h = render(m);
  ASSERT_CONTAINS(h, "class=\"pill ok\"");
  ASSERT_CONTAINS(h, "<b>2880</b>");
  ASSERT_NOT_CONTAINS(h, "<div class=\"warn\">");
}

static void test_auto_cycle_checkbox_state(void) {
  ConfigPageModel m = baseModel();
  m.cfg.auto_cycle_enabled = true;
  ASSERT_CONTAINS(render(m), "name=\"auto_cycle\" value=\"1\" checked");
  m.cfg.auto_cycle_enabled = false;
  ASSERT_NOT_CONTAINS(render(m), "name=\"auto_cycle\" value=\"1\" checked");
}

static void test_footer_shows_the_firmware_version(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::PL;
  std::string h = render(m);
  // Native builds get no injected version -> the fallback.
  ASSERT_CONTAINS(h, "wersja <span id=\"ver\">dev</span>");
  m.cfg.web_language = Language::EN;
  ASSERT_CONTAINS(render(m), "version <span id=\"ver\">dev</span>");
}

static void test_backup_tab_has_download_and_restore(void) {
  ConfigPageModel m = baseModel();
  m.tab = SettingsTab::Backup;
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "data-t=\"backup\" class=\"on\"");
  ASSERT_CONTAINS(h, "<section data-tab=\"backup\">");
  // Download: GET form with the secrets checkbox (on by default).
  ASSERT_CONTAINS(h, "<form id=\"bk\" method=\"GET\" action=\"/backup.json\">");
  ASSERT_CONTAINS(h, "name=\"secrets\" value=\"1\" form=\"bk\" checked");
  // Restore: multipart upload in its own form.
  ASSERT_CONTAINS(h, "<form id=\"rs\" method=\"POST\" action=\"/restore\" " "enctype=\"multipart/form-data\">");
  ASSERT_CONTAINS(h, "name=\"backup_file\" form=\"rs\"");
  // The settings Save button means nothing on this tab.
  ASSERT_CONTAINS(h, "id=\"save\" hidden>");

  m.tab = SettingsTab::Location;
  ASSERT_NOT_CONTAINS(render(m), "id=\"save\" hidden>");
}

static void test_quota_status_text(void) {
  bool exhausted = true;
  OpenSkyQuota q;  // Unknown
  TEST_ASSERT_EQUAL_STRING("Credits left: not known yet, shown after the first request.",
                           quotaStatusText(q, 0, Language::EN, &exhausted).c_str());
  TEST_ASSERT_FALSE(exhausted);

  q.state = OpenSkyQuota::State::Available;
  q.remaining = 1234;
  TEST_ASSERT_EQUAL_STRING("Pozostało dziś kredytów: 1234 (z ostatniego zapytania)",
                           quotaStatusText(q, 0, Language::PL, &exhausted).c_str());
  TEST_ASSERT_FALSE(exhausted);

  q.state = OpenSkyQuota::State::Exhausted;
  q.reset_in_s = 25012;
  TEST_ASSERT_EQUAL_STRING("Daily limit reached: renews in about 6 h 57 min.",
                           quotaStatusText(q, 25012, Language::EN, &exhausted).c_str());
  TEST_ASSERT_TRUE(exhausted);
  TEST_ASSERT_EQUAL_STRING(
      "Dzienny limit wyczerpany: powinien się już odnowić, płytka sprawdzi to przy następnej próbie.",
      quotaStatusText(q, 0, Language::PL, &exhausted).c_str());
  q.reset_in_s = 0;
  TEST_ASSERT_EQUAL_STRING("Daily limit reached: OpenSky didn't say when it renews.",
                           quotaStatusText(q, 0, Language::EN, &exhausted).c_str());
}

static void test_opensky_tab_shows_the_quota(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.quota.state = OpenSkyQuota::State::Available;
  m.quota.remaining = 3971;
  std::string h = render(m);
  ASSERT_CONTAINS(h, "<div class=\"stat quota\" id=\"quota\">Credits left today: 3971");

  m.quota.state = OpenSkyQuota::State::Exhausted;
  m.quota.reset_in_s = 7200;
  m.quotaResetInS = 3600;
  h = render(m);
  ASSERT_CONTAINS(h, "<div class=\"stat quota warn\" id=\"quota\">Daily limit reached: " "renews in about 1 h.");
}

static void test_firmware_tab(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.tab = SettingsTab::Firmware;
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "<section data-tab=\"firmware\">");
  ASSERT_CONTAINS(h, "id=\"tab\" value=\"firmware\"");
  ASSERT_CONTAINS(h, "<form id=\"fw\" method=\"POST\" action=\"/update\" " "enctype=\"multipart/form-data\">");
  ASSERT_CONTAINS(h, "name=\"firmware\" form=\"fw\"");
  ASSERT_CONTAINS(h, "Installed version: <b id=\"fwv\">dev</b>");
  ASSERT_CONTAINS(h, kReleasesUrl);
  ASSERT_CONTAINS(h, "id=\"save\" hidden>");  // nothing to save here
  ASSERT_CONTAINS(h, ">Firmware</button>");
}

// ---- Updates from GitHub (release_check), first card on the Firmware tab

static ConfigPageModel updateModel(bool withPre) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.tab = SettingsTab::Firmware;
  m.update = updateDemoState(withPre);
  m.installedVersion = m.update.installed;
  m.cfg.update_show_prerelease = withPre;
  return m;
}

static void test_update_card_offers_the_newer_release(void) {
  const std::string h = render(updateModel(false));
  ASSERT_CONTAINS(h, "<div class=\"card\" id=\"gh\"><h2>Updates from GitHub</h2>");
  ASSERT_CONTAINS(h, "Installed version: <b id=\"fwv\">v1.2.0</b>");
  ASSERT_CONTAINS(h, "Latest release: <b>v1.3.0</b> \xC2\xB7 published 20.10.2026");
  ASSERT_CONTAINS(h, "href=\"https://github.com/alasota/CYD-Flight-Tracker/releases/tag/v1.3.0\"");
  ASSERT_CONTAINS(h, "<span class=\"pill ok\" id=\"ups\"><span class=\"dot\"></span>" "A newer version is available.</span>");
  ASSERT_CONTAINS(h, "checked 24.10.2026 09:11");
  ASSERT_CONTAINS(h, "<button form=\"ui\" class=\"wide\" id=\"upi\"");
  ASSERT_CONTAINS(h, ">Install v1.3.0</button>");
  ASSERT_CONTAINS(h, "<form id=\"ui\" method=\"POST\" action=\"/update/install\">" "<input type=\"hidden\" name=\"tag\" value=\"v1.3.0\"></form>");
  ASSERT_CONTAINS(h, "<form id=\"uc\" method=\"POST\" action=\"/update/check\">");
  ASSERT_CONTAINS(h, ">Check now</button>");
  ASSERT_CONTAINS(h, "version <span id=\"ver\">v1.2.0</span>");
  // Development versions: the switch is there, off, and nothing else.
  ASSERT_CONTAINS(h, "name=\"on\" value=\"1\" form=\"upr\" " "onchange=\"this.form.submit()\"></label>");
  ASSERT_NOT_CONTAINS(h, "id=\"upre\"");
  ASSERT_NOT_CONTAINS(h, "id=\"upip\"");
}

static void test_update_card_install_button_only_when_installable(void) {
  ConfigPageModel m = updateModel(false);
  for (UpdateStatus st : {UpdateStatus::UpToDate, UpdateStatus::NoChecksum, UpdateStatus::NoAsset,
                          UpdateStatus::CheckFailed, UpdateStatus::RateLimited,
                          UpdateStatus::NeverChecked}) {
    m.update.status = st;
    const std::string h = render(m);
    ASSERT_NOT_CONTAINS(h, "id=\"upi\"");
    ASSERT_CONTAINS(h, webTr(updateStatusMessage(st), Language::EN));
  }
  m.update.status = UpdateStatus::BackToRelease;
  ASSERT_CONTAINS(render(m), ">Go back to release v1.3.0</button>");
  m.update.status = UpdateStatus::DevBuild;
  ASSERT_CONTAINS(render(m), ">Install v1.3.0</button>");
  // The installed version is never offered again.
  m.installedVersion = "v1.3.0";
  ASSERT_NOT_CONTAINS(render(m), "id=\"upi\"");
}

static void test_update_card_development_channel(void) {
  ConfigPageModel m = updateModel(true);
  std::string h = render(m);
  ASSERT_CONTAINS(h, "onchange=\"this.form.submit()\" checked></label>");
  ASSERT_CONTAINS(h, "id=\"upre\"");
  ASSERT_CONTAINS(h, "Latest development version: <b>v1.4.0-rc.1</b>");
  ASSERT_CONTAINS(h, "A test version");
  ASSERT_CONTAINS(h, ">Install v1.4.0-rc.1</button>");
  ASSERT_CONTAINS(h, "<form id=\"uip\" method=\"POST\" action=\"/update/install\">" "<input type=\"hidden\" name=\"tag\" value=\"v1.4.0-rc.1\">");
  // Switch off: the pre-release isn't shown even if the state still has it.
  m.cfg.update_show_prerelease = false;
  h = render(m);
  ASSERT_NOT_CONTAINS(h, "id=\"upre\"");
  ASSERT_NOT_CONTAINS(h, ">Install v1.4.0-rc.1</button>");
  // Already running it: no button, a note instead.
  m = updateModel(true);
  m.installedVersion = "v1.4.0-rc.1";
  h = render(m);
  ASSERT_NOT_CONTAINS(h, "id=\"upip\"");
  ASSERT_CONTAINS(h, "v1.4.0-rc.1 \xC2\xB7 installed");
}

static void test_update_card_shows_a_failed_install(void) {
  ConfigPageModel m = updateModel(false);
  m.update.lastInstall = OtaResult::ChecksumMismatch;
  m.update.lastInstallTag = "v1.3.0";
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "The last install failed (v1.3.0): Not updated: the downloaded " "file's SHA-256 does not match");
}

static void test_update_card_escapes_what_came_from_the_network(void) {
  ConfigPageModel m = updateModel(true);
  m.update.latest.tag = "v1.3.0\"><script>";
  m.update.latestPre.tag = "<b>";
  const std::string h = render(m);
  ASSERT_NOT_CONTAINS(h, "v1.3.0\"><script>");
  ASSERT_CONTAINS(h, "<b>v1.3.0&quot;&gt;&lt;script&gt;</b>");
}

static void test_update_check_switch_on_general(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  ASSERT_CONTAINS(render(m), "name=\"update_check\" value=\"1\" checked");
  m.cfg.update_check_enabled = false;
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "name=\"update_check\" value=\"1\">");
  ASSERT_NOT_CONTAINS(h, "name=\"update_check\" value=\"1\" checked");
}

// Writes the demo Firmware tab (PL, like the other screenshots) for
// tools/screenshots.py when SCREENSHOT_OUT names a folder; does nothing in a
// normal test run.
static void test_render_firmware_tab_demo(void) {
  const char *dir = std::getenv("SCREENSHOT_OUT");
  if (dir == nullptr || *dir == '\0') {
    TEST_IGNORE_MESSAGE("SCREENSHOT_OUT not set");
    return;
  }
  for (bool withPre : {false, true}) {
    ConfigPageModel m = updateModel(withPre);
    m.cfg.web_language = Language::PL;
    m.cfg.web_password = "demo-password";  // no "no password" warning in the picture
    const std::string path =
        std::string(dir) + (withPre ? "/web_firmware_dev.html" : "/web_firmware.html");
    std::FILE *f = std::fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    const std::string h = render(m);
    std::fwrite(h.data(), 1, h.size(), f);
    std::fclose(f);
  }
}

// The General and OpenSky tabs for docs/screenshots/web_general.png /
// web_opensky.png (spec/03 §5.8): the Display card with the demo values
// (automatic brightness, sunset to sunrise at 10 %, the night pause on, so
// the OpenSky tab's estimate counts the night).
static ConfigPageModel displayDemoModel(SettingsTab tab) {
  ConfigPageModel m = baseModel();
  m.tab = tab;
  m.cfg.web_language = Language::PL;
  m.cfg.web_password = "demo-password";
  m.cfg.home_configured = true;
  m.cfg.opensky_client_id = "demo-client";
  m.cfg.opensky_client_secret = "demo-secret";
  m.cfg.night_pause_polling = true;
  m.quota.state = OpenSkyQuota::State::Available;
  m.quota.remaining = 2814;
  m.light.raw = 0;
  m.light.avg = 0.0f;
  m.light.pct = 100;
  m.light.pwm = 255;
  m.light.synced = true;
  m.light.sun_known = true;
  m.light.sun_local.sunrise_min = 7 * 60 + 17;
  m.light.sun_local.sunset_min = 17 * 60 + 23;
  return m;
}

static void test_render_general_and_opensky_demo(void) {
  const char *dir = std::getenv("SCREENSHOT_OUT");
  if (dir == nullptr || *dir == '\0') {
    TEST_IGNORE_MESSAGE("SCREENSHOT_OUT not set");
    return;
  }
  const struct {
    SettingsTab tab;
    const char *file;
  } pages[] = {{SettingsTab::General, "/web_general.html"}, {SettingsTab::OpenSky, "/web_opensky.html"}};
  for (const auto &p : pages) {
    std::FILE *f = std::fopen((std::string(dir) + p.file).c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    const std::string h = render(displayDemoModel(p.tab));
    std::fwrite(h.data(), 1, h.size(), f);
    std::fclose(f);
  }
}

static void test_restarting_page_reloads_itself(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.tab = SettingsTab::Firmware;
  m.hasMessage = true;
  m.message = WebStr::OtaOk;
  m.messageDetail = "v1.2.0 â v1.3.0";
  ASSERT_NOT_CONTAINS(render(m), "location.href='/?tab=firmware'");
  m.restarting = true;
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "Firmware updated: v1.2.0");
  ASSERT_CONTAINS(h, "The board is restarting");
  ASSERT_CONTAINS(h, "location.href='/?tab=firmware'");
}

static void test_saved_secret_never_reaches_the_page(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.cfg.opensky_client_id = "my-id";
  m.cfg.opensky_client_secret = "TOP-SECRET-VALUE";
  std::string h = render(m);
  ASSERT_NOT_CONTAINS(h, "TOP-SECRET-VALUE");
  ASSERT_CONTAINS(h, "name=\"client_secret\" autocomplete=\"off\" value=\"\" " "data-saved=\"1\" placeholder=\"saved");
  ASSERT_CONTAINS(h, "name=\"clear_credentials\"");

  m.cfg.opensky_client_secret.clear();
  h = render(m);
  ASSERT_NOT_CONTAINS(h, "data-saved");
  ASSERT_NOT_CONTAINS(h, "name=\"clear_credentials\"");
}

static void test_panel_password_card(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  std::string h = render(m);
  // No password: warned on General and on Firmware, nothing to remove.
  ASSERT_CONTAINS(h, "The panel has no password");
  const size_t first = h.find("The panel has no password");
  ASSERT_CONTAINS(h.substr(first + 1), "The panel has no password");
  ASSERT_CONTAINS(h, "name=\"new_password\" autocomplete=\"new-password\"");
  ASSERT_CONTAINS(h, "name=\"new_password2\"");
  ASSERT_NOT_CONTAINS(h, "name=\"clear_password\"");

  m.cfg.web_password = "hunter22-secret";
  h = render(m);
  ASSERT_NOT_CONTAINS(h, "hunter22-secret");  // never echoed
  ASSERT_NOT_CONTAINS(h, "The panel has no password");
  ASSERT_CONTAINS(h, "Password set");
  ASSERT_CONTAINS(h, "name=\"clear_password\"");
}


// ---- the device-alert card above the tabs (spec 08 §3.6) -----------------------

static ConfigPageModel alertModel(Language lang) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = lang;
  const AlertDemo demo = alertDemoState();
  m.alerts = demo.alerts;
  m.alertDetails = demo.details;
  return m;
}

static void test_no_alerts_no_card(void) {
  const std::string h = render(baseModel());
  ASSERT_NOT_CONTAINS(h, "card alert");
  ASSERT_NOT_CONTAINS(h, "id=\"alerts\"");
}

static void test_each_alert_renders_title_why_action_link(void) {
  for (Language lang : {Language::PL, Language::EN}) {
    for (int i = 0; i < kAlertCount; ++i) {
      const AlertId id = static_cast<AlertId>(i);
      ConfigPageModel m = alertModel(lang);
      m.alerts = AlertSet{};
      m.alerts.set(id);
      const std::string h = render(m);
      const AlertDef &def = alertDef(id);
      ASSERT_CONTAINS(h, std::string("id=\"alert-") + def.code + "\"");
      ASSERT_CONTAINS(h, webTr(def.webTitle, lang));
      ASSERT_CONTAINS(h, webTr(def.webAction, lang));
      ASSERT_CONTAINS(h, htmlEscape(alertWebWhy(id, m.alertDetails, 0, lang)));
      ASSERT_CONTAINS(h, "href=\"/?tab=opensky\"");
      ASSERT_CONTAINS(h, webTr(WebStr::AlertCardTitle, lang));
      const std::string cls = alertWebClass(def.severity);
      ASSERT_CONTAINS(h, "class=\"card alert " + cls + "\"");
      ASSERT_CONTAINS(h, "class=\"ai " + cls + "\"");
    }
  }
}

static void test_alert_card_errors_first_and_worst_colour(void) {
  const std::string h = render(alertModel(Language::EN));
  ASSERT_CONTAINS(h, "class=\"card alert err\"");  // an error is among them
  const size_t login = h.find("alert-opensky_login_failed");
  const size_t budget = h.find("alert-opensky_budget_over_limit");
  TEST_ASSERT_TRUE(login != std::string::npos && budget != std::string::npos && login < budget);
  ConfigPageModel w = alertModel(Language::EN);
  w.alerts = AlertSet{};
  w.alerts.set(AlertId::OpenSkyBudgetOverLimit);
  ASSERT_CONTAINS(render(w), "class=\"card alert warn\"");
}

static void test_alert_card_is_above_the_tabs_on_every_tab(void) {
  for (SettingsTab t : {SettingsTab::Location, SettingsTab::OpenSky, SettingsTab::General,
                        SettingsTab::Backup, SettingsTab::Firmware}) {
    ConfigPageModel m = alertModel(Language::PL);
    m.tab = t;
    const std::string h = render(m);
    const size_t card = h.find("id=\"alerts\"");
    const size_t tabs = h.find("class=\"tabs\"");
    TEST_ASSERT_TRUE(card != std::string::npos && tabs != std::string::npos && card < tabs);
  }
}

// docs/screenshots/web_alert.png (tools/screenshots.py, SCREENSHOT_OUT).
static void test_render_alert_demo(void) {
  const char *dir = std::getenv("SCREENSHOT_OUT");
  if (dir == nullptr || *dir == '\0') {
    TEST_IGNORE_MESSAGE("SCREENSHOT_OUT not set");
    return;
  }
  ConfigPageModel m = alertModel(Language::PL);
  m.cfg.web_password = "demo-password";
  m.tab = SettingsTab::OpenSky;
  // The demo's login alert, also on the OpenSky tab: red pill + warning.
  m.cfg.opensky_client_id = "demo-client";
  m.cfg.opensky_client_secret = "demo-secret";
  m.auth = onCredentials(OpenSkyAuthState{}, credentialsHash("demo-client", "demo-secret"), true);
  m.auth.mode = OpenSkyAuthState::Mode::LoginFailed;
  m.auth.lastHttpCode = 401;
  m.auth.failedSinceMs = m.nowMs - 3u * 60000u;  // wraps if nowMs is 0: wrap-safe
  m.auth.nextTokenTryMs = m.auth.failedSinceMs + kLoginRetryMs;
  m.nowEpoch = 1792938720 + 180;  // 25.10.2026 14:35 local
  std::FILE *f = std::fopen((std::string(dir) + "/web_alert.html").c_str(), "wb");
  TEST_ASSERT_NOT_NULL(f);
  const std::string h = render(m);
  std::fwrite(h.data(), 1, h.size(), f);
  std::fclose(f);
}


// ---- OpenSky login state on the OpenSky tab (spec 06 §3.3, §3.4) ---------------

constexpr std::time_t kSince = 1792938720;  // 25.10.2026 14:32 (local, as formatDate() takes it)
constexpr uint32_t kNowMs = 5000000u;

// Credentials in cfg and an auth state for exactly these credentials.
static ConfigPageModel authModel(OpenSkyAuthState::Mode mode, Language lang) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = lang;
  m.tab = SettingsTab::OpenSky;
  m.nowMs = kNowMs;
  if (mode == OpenSkyAuthState::Mode::NoCredentials) return m;
  m.cfg.opensky_client_id = "id";
  m.cfg.opensky_client_secret = "secret";
  m.auth = onCredentials(OpenSkyAuthState{}, credentialsHash("id", "secret"), true);
  if (mode == OpenSkyAuthState::Mode::LoginFailed) {
    // Rejected with 401 three minutes ago (14:32), the clock now says 14:35.
    m.auth.mode = OpenSkyAuthState::Mode::LoginFailed;
    m.auth.lastHttpCode = 401;
    m.auth.failedSinceMs = kNowMs - 3u * 60000u;
    m.auth.nextTokenTryMs = m.auth.failedSinceMs + kLoginRetryMs;  // 27 min from now
    m.nowEpoch = kSince + 180;
  }
  return m;
}

// The opening tag of the #daily estimate div.
static std::string dailyTag(const std::string &h) {
  const size_t at = h.find("id=\"daily\"");
  if (at == std::string::npos) return "";
  const size_t start = h.rfind('<', at);
  return h.substr(start, h.find('>', at) - start + 1);
}

// Scenario Outline: Pigułka na zakładce OpenSky odpowiada stanowi logowania
static void test_opensky_pill_per_auth_state(void) {
  using Mode = OpenSkyAuthState::Mode;
  struct Row {
    Mode mode;
    Language lang;
    const char *cls;
    const char *text;
  };
  const Row rows[] = {
      {Mode::NoCredentials, Language::PL, "pill", "Tryb anonimowy"},
      {Mode::NoCredentials, Language::EN, "pill", "Anonymous mode"},
      {Mode::Account, Language::PL, "pill ok", "Konto OpenSky skonfigurowane"},
      {Mode::Account, Language::EN, "pill ok", "OpenSky account configured"},
      {Mode::LoginFailed, Language::PL, "pill err", "Błąd logowania — tryb anonimowy"},
      {Mode::LoginFailed, Language::EN, "pill err", "Login failed — anonymous mode"},
  };
  for (const Row &r : rows) {
    const std::string h = render(authModel(r.mode, r.lang));
    ASSERT_CONTAINS(h, std::string("<span class=\"") + r.cls + "\"><span class=\"dot\"></span>" +
                           r.text + "</span>");
  }
}

// Scenario: W stanie LoginFailed pod pigułką jest ostrzeżenie z kodem HTTP, czasem i następną próbą
static void test_login_failed_warning_with_code_time_and_retry(void) {
  const ConfigPageModel m = authModel(OpenSkyAuthState::Mode::LoginFailed, Language::PL);
  const std::string h = render(m);
  ASSERT_CONTAINS(h,
                  "<div class=\"stat quota warn\" id=\"loginwarn\">OpenSky odrzucił client_id / "
                  "client_secret (HTTP 401, od 25.10.2026 14:32). Urządzenie działa w trybie "
                  "anonimowym: limit 400 kredytów dziennie zamiast 4000. Sprawdź dane na stronie "
                  "konta OpenSky i zapisz je ponownie. Następna próba logowania za 27 min.</div>");
  // Right below the pill, before the client id field.
  const size_t pill = h.find("class=\"pill err\"");
  const size_t warn = h.find("id=\"loginwarn\"");
  const size_t cid = h.find("name=\"client_id\"");
  TEST_ASSERT_TRUE(pill != std::string::npos && warn != std::string::npos && pill < warn &&
                   warn < cid);
  // The generic "without an account: 400" hint would only repeat the warning.
  ASSERT_NOT_CONTAINS(h, "Bez konta wyświetlacz działa anonimowo");
  ASSERT_NOT_CONTAINS(render(authModel(OpenSkyAuthState::Mode::LoginFailed, Language::EN)),
                      "Without an account the display runs anonymously");

  ASSERT_STR_EQ("OpenSky rejected the client_id / client_secret (HTTP 401, since 25.10.2026 "
                "14:32). The device runs anonymously: a limit of 400 credits a day instead of "
                "4000. Check them on your OpenSky account page and save them again. Next login "
                "attempt in 27 min.",
                loginFailedDetailText(m.auth, m.nowMs, m.nowEpoch, Language::EN));
}

// Scenario: Bez zsynchronizowanego zegara ostrzeżenie pomija czas „od”
static void test_login_failed_warning_without_clock(void) {
  for (Language lang : {Language::PL, Language::EN}) {
    ConfigPageModel m = authModel(OpenSkyAuthState::Mode::LoginFailed, lang);
    m.nowEpoch = 0;
    const std::string text = loginFailedDetailText(m.auth, m.nowMs, 0, lang);
    ASSERT_CONTAINS(text, "(HTTP 401).");
    ASSERT_NOT_CONTAINS(text, "2026");
    ASSERT_NOT_CONTAINS(text, "1970");
    ASSERT_NOT_CONTAINS(text, ":32");
    const std::string h = render(m);
    ASSERT_CONTAINS(h, "id=\"loginwarn\">" + text + "</div>");
  }
}

// Scenario: Ostrzeżenia o logowaniu nie ma w stanach Account i NoCredentials
static void test_no_login_warning_unless_failed(void) {
  for (OpenSkyAuthState::Mode mode :
       {OpenSkyAuthState::Mode::Account, OpenSkyAuthState::Mode::NoCredentials}) {
    for (Language lang : {Language::PL, Language::EN}) {
      const std::string h = render(authModel(mode, lang));
      ASSERT_NOT_CONTAINS(h, "loginwarn");
      ASSERT_NOT_CONTAINS(h, "class=\"pill err\"");
      ASSERT_NOT_CONTAINS(h, "OpenSky odrzucił");
      ASSERT_NOT_CONTAINS(h, "OpenSky rejected");
      // ...and the generic hint about the account's trade-off is there.
      ASSERT_CONTAINS(h, lang == Language::PL ? "Bez konta wyświetlacz działa anonimowo"
                                              : "Without an account the display runs anonymously");
    }
  }
}

// Scenario: W stanie LoginFailed szacunek kredytów dziennie porównywany jest z limitem 400
static void test_daily_limit_is_anonymous_when_login_failed(void) {
  ConfigPageModel m = authModel(OpenSkyAuthState::Mode::LoginFailed, Language::EN);
  m.cfg.poll_interval_s = 30;  // 2880 credits a day
  std::string h = render(m);
  ASSERT_CONTAINS(h, "<b>2880</b> credits a day");
  ASSERT_CONTAINS(h, "limit 400<div class=\"warn\">");

  m = authModel(OpenSkyAuthState::Mode::Account, Language::EN);
  m.cfg.poll_interval_s = 30;
  h = render(m);
  ASSERT_CONTAINS(h, "<b>2880</b> credits a day");
  ASSERT_CONTAINS(h, "limit 4000</div>");
  ASSERT_NOT_CONTAINS(h, "<div class=\"warn\">");
}

// Scenario: Świeżo zapisane inne dane logowania od razu pokazują zieloną pigułkę
static void test_just_saved_credentials_show_account(void) {
  ConfigPageModel m = authModel(OpenSkyAuthState::Mode::LoginFailed, Language::EN);
  m.auth.credentialsHash = credentialsHash("old-id", "old-secret");  // learnt for the old ones
  m.cfg.poll_interval_s = 30;
  const std::string h = render(m);
  ASSERT_CONTAINS(h, "<span class=\"pill ok\"><span class=\"dot\"></span>OpenSky account configured");
  ASSERT_CONTAINS(h, "limit 4000</div>");
  ASSERT_NOT_CONTAINS(h, "loginwarn");
  ASSERT_NOT_CONTAINS(h, "class=\"pill err\"");
  ASSERT_NOT_CONTAINS(dailyTag(h), "data-lf");
}

// Edge case: minutes to the next try round up and never go below 1.
static void test_login_failed_detail_minutes_round_up(void) {
  OpenSkyAuthState s = authModel(OpenSkyAuthState::Mode::LoginFailed, Language::PL).auth;
  s.nextTokenTryMs = kNowMs + 26u * 60000u + 1000u;  // 26 min 1 s left
  ASSERT_CONTAINS(loginFailedDetailText(s, kNowMs, 0, Language::PL), "logowania za 27 min.");
  s.nextTokenTryMs = kNowMs + 60000u;  // exactly 1 min
  ASSERT_CONTAINS(loginFailedDetailText(s, kNowMs, 0, Language::EN), "attempt in 1 min.");
  s.nextTokenTryMs = kNowMs;  // due right now
  ASSERT_CONTAINS(loginFailedDetailText(s, kNowMs, 0, Language::EN), "attempt in 1 min.");
  s.nextTokenTryMs = kNowMs - 5000u;  // already past (the poll that retries hasn't run yet)
  ASSERT_CONTAINS(loginFailedDetailText(s, kNowMs, 0, Language::EN), "attempt in 1 min.");
}

// Edge case: millis() wrapped between the rejection and now — "since" and the minutes stay right.
static void test_login_failed_detail_across_millis_rollover(void) {
  OpenSkyAuthState s = authModel(OpenSkyAuthState::Mode::LoginFailed, Language::EN).auth;
  const uint32_t now = 2u * 60000u;                    // 2 min after the wrap
  s.failedSinceMs = UINT32_MAX - 60000u + 1u;          // 1 min before the wrap -> 3 min ago
  s.nextTokenTryMs = s.failedSinceMs + kLoginRetryMs;  // 27 min from now
  const std::string text = loginFailedDetailText(s, now, kSince + 180, Language::EN);
  ASSERT_CONTAINS(text, "(HTTP 401, since 25.10.2026 14:32).");
  ASSERT_CONTAINS(text, "attempt in 27 min.");
}

// Edge case (review): a series older than kLoginFailedSinceMaxMs — or one
// marked stale because millis() may have wrapped past it — leaves "since" out
// instead of showing a date weeks too late.
static void test_login_failed_detail_omits_since_when_too_old(void) {
  OpenSkyAuthState s = authModel(OpenSkyAuthState::Mode::LoginFailed, Language::EN).auth;
  const uint32_t now = s.failedSinceMs + kLoginFailedSinceMaxMs;  // right at the bound: shown
  s.nextTokenTryMs = now + 60000u;
  ASSERT_CONTAINS(loginFailedDetailText(s, now, kSince + 600, Language::EN), "(HTTP 401, since ");
  // One ms past it: left out.
  std::string text = loginFailedDetailText(s, now + 1u, kSince + 600, Language::EN);
  ASSERT_CONTAINS(text, "(HTTP 401).");
  ASSERT_NOT_CONTAINS(text, "since");
  // Stale (the age looks small again after a wrap): left out too.
  s.failedSinceStale = true;
  text = loginFailedDetailText(s, s.failedSinceMs + 60000u, kSince + 600, Language::PL);
  ASSERT_CONTAINS(text, "(HTTP 401).");
  ASSERT_NOT_CONTAINS(text, ", od ");
}

// Edge case: the live estimate's script learns about LoginFailed through data-lf.
static void test_login_failed_marks_live_estimate(void) {
  ASSERT_CONTAINS(dailyTag(render(authModel(OpenSkyAuthState::Mode::LoginFailed, Language::PL))),
                  "data-lf=\"1\"");
  for (OpenSkyAuthState::Mode mode :
       {OpenSkyAuthState::Mode::Account, OpenSkyAuthState::Mode::NoCredentials}) {
    ASSERT_NOT_CONTAINS(dailyTag(render(authModel(mode, Language::PL))), "data-lf");
  }
}

// Edge case: the red pill has its own colour rule.
static void test_login_failed_pill_style(void) {
  const std::string h = render(baseModel());
  ASSERT_CONTAINS(h, ".pill.err{color:var(--err)}");
  ASSERT_CONTAINS(h, ".pill.err .dot{background:var(--err)}");
}

}  // namespace

void run_config_page_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_quota_status_text);
  RUN_TEST(test_opensky_tab_shows_the_quota);
  RUN_TEST(test_backup_tab_has_download_and_restore);
  RUN_TEST(test_footer_shows_the_firmware_version);
  RUN_TEST(test_tab_ids_round_trip);
  RUN_TEST(test_daily_credits);
  RUN_TEST(test_daily_credits_rounding_and_zero_interval);
  RUN_TEST(test_daily_credits_with_a_night_pause);
  RUN_TEST(test_paused_minutes_follow_the_settings);
  RUN_TEST(test_light_state_text);
  RUN_TEST(test_display_card);
  RUN_TEST(test_credentials_need_both_fields);
  RUN_TEST(test_html_escape);
  RUN_TEST(test_page_is_complete_and_never_emits_empty_chunks);
  RUN_TEST(test_page_has_the_project_favicon);
  RUN_TEST(test_all_form_fields_present);
  RUN_TEST(test_only_the_active_tab_is_visible);
  RUN_TEST(test_web_language_switches_the_page_text);
  RUN_TEST(test_selects_reflect_both_languages_independently);
  RUN_TEST(test_user_values_are_escaped);
  RUN_TEST(test_message_and_detail);
  RUN_TEST(test_status_and_daily_credit_readout);
  RUN_TEST(test_auto_cycle_checkbox_state);
  RUN_TEST(test_firmware_tab);
  RUN_TEST(test_restarting_page_reloads_itself);
  RUN_TEST(test_update_card_offers_the_newer_release);
  RUN_TEST(test_update_card_install_button_only_when_installable);
  RUN_TEST(test_update_card_development_channel);
  RUN_TEST(test_update_card_shows_a_failed_install);
  RUN_TEST(test_update_card_escapes_what_came_from_the_network);
  RUN_TEST(test_update_check_switch_on_general);
  RUN_TEST(test_render_firmware_tab_demo);
  RUN_TEST(test_render_general_and_opensky_demo);
  RUN_TEST(test_saved_secret_never_reaches_the_page);
  RUN_TEST(test_panel_password_card);
  RUN_TEST(test_no_alerts_no_card);
  RUN_TEST(test_each_alert_renders_title_why_action_link);
  RUN_TEST(test_alert_card_errors_first_and_worst_colour);
  RUN_TEST(test_alert_card_is_above_the_tabs_on_every_tab);
  RUN_TEST(test_render_alert_demo);
  RUN_TEST(test_opensky_pill_per_auth_state);
  RUN_TEST(test_login_failed_warning_with_code_time_and_retry);
  RUN_TEST(test_login_failed_warning_without_clock);
  RUN_TEST(test_no_login_warning_unless_failed);
  RUN_TEST(test_daily_limit_is_anonymous_when_login_failed);
  RUN_TEST(test_just_saved_credentials_show_account);
  RUN_TEST(test_login_failed_detail_minutes_round_up);
  RUN_TEST(test_login_failed_detail_across_millis_rollover);
  RUN_TEST(test_login_failed_detail_omits_since_when_too_old);
  RUN_TEST(test_login_failed_marks_live_estimate);
  RUN_TEST(test_login_failed_pill_style);
}
