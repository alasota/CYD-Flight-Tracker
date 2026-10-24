#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "config_page.h"

void setUp(void) {}
void tearDown(void) {}

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

bool contains(const std::string &h, const char *needle) { return h.find(needle) != std::string::npos; }

ConfigPageModel baseModel() {
  ConfigPageModel m;
  m.cfg = defaultConfig();
  m.cfg.home_lat = 52.2297f;
  m.cfg.home_lon = 21.0122f;
  return m;
}

}  // namespace

// ---- pure helpers -----------------------------------------------------------

static void test_tab_ids_round_trip(void) {
  for (SettingsTab t : {SettingsTab::Location, SettingsTab::OpenSky, SettingsTab::General,
                        SettingsTab::Backup, SettingsTab::Firmware}) {
    TEST_ASSERT_TRUE(settingsTabFromArg(settingsTabId(t)) == t);
  }
  TEST_ASSERT_TRUE(settingsTabFromArg("") == SettingsTab::Location);
  TEST_ASSERT_TRUE(settingsTabFromArg("nonsense") == SettingsTab::Location);
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
  TEST_ASSERT_TRUE(contains(h, "<option value=\"auto\" selected>automatic (light sensor)"));
  TEST_ASSERT_TRUE(contains(h, "<option value=\"sun\" selected>sunset to sunrise"));
  TEST_ASSERT_TRUE(contains(h, "<div id=\"blauto\">"));
  TEST_ASSERT_TRUE(contains(h, "<div id=\"blfix\" hidden>"));
  TEST_ASSERT_TRUE(contains(h, "<div id=\"ntsch\" hidden>"));
  TEST_ASSERT_TRUE(contains(h, "<div id=\"ntopt\">"));
  TEST_ASSERT_TRUE(contains(h, "name=\"bl_min\" min=\"5\" max=\"100\" value=\"10\""));
  TEST_ASSERT_TRUE(contains(h, "name=\"night_pct\" min=\"0\" max=\"100\" value=\"10\""));
  TEST_ASSERT_TRUE(contains(h, "name=\"night_wake_s\" min=\"10\" max=\"600\" value=\"60\""));
  TEST_ASSERT_TRUE(contains(h, "name=\"night_pause\" value=\"1\">"));
  TEST_ASSERT_FALSE(contains(h, "name=\"night_pause\" value=\"1\" checked"));
  TEST_ASSERT_TRUE(contains(h, "Sunrise and sunset appear once the clock is synced."));

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
  TEST_ASSERT_TRUE(contains(h, "<option value=\"fixed\" selected>fixed"));
  TEST_ASSERT_TRUE(contains(h, "<div id=\"blfix\">"));
  TEST_ASSERT_TRUE(contains(h, "<div id=\"ntsch\">"));
  TEST_ASSERT_TRUE(contains(h, "type=\"time\" name=\"night_from\" value=\"22:30\""));
  TEST_ASSERT_TRUE(contains(h, "name=\"night_pause\" value=\"1\" checked"));
  TEST_ASSERT_TRUE(contains(h, "<b id=\"lr\">1234</b>"));
  TEST_ASSERT_TRUE(contains(h, "<b id=\"lp\">35</b> %"));
  TEST_ASSERT_TRUE(contains(h, "Sun today: sunrise 07:17, sunset 17:23"));
  // 22:30-06:30 = 480 min at 15 s = 1920 fewer requests; the OpenSky tab counts them.
  TEST_ASSERT_TRUE(contains(h, "<b id=\"ntsv\">1920</b> fewer requests tonight"));
  TEST_ASSERT_TRUE(contains(h, "data-night=\"480\">≈ <b>3840</b>"));

  m.cfg.night_mode = NightMode::Off;
  m.cfg.home_configured = false;
  h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<div id=\"ntopt\" hidden>"));
  TEST_ASSERT_TRUE(contains(h, "Set the location to work out sunrise and sunset."));
}

static void test_daily_limits(void) {
  TEST_ASSERT_EQUAL_UINT32(400u, openSkyDailyCreditLimit(false));
  TEST_ASSERT_EQUAL_UINT32(4000u, openSkyDailyCreditLimit(true));
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
  TEST_ASSERT_TRUE(contains(h, "<!DOCTYPE html>"));
  TEST_ASSERT_TRUE(contains(h, "</html>"));
}

static void test_page_has_the_project_favicon(void) {
  const std::string h = render(baseModel());
  TEST_ASSERT_TRUE(contains(h, "<link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,"));
  TEST_ASSERT_TRUE(contains(h, "fill='%23ff9966'"));      // peach plane, '#' escaped
  TEST_ASSERT_TRUE(h.find("<link rel=\"icon\"") < h.find("</head>"));
}

static void test_all_form_fields_present(void) {
  const std::string h = render(baseModel());
  const char *names[] = {"name=\"lat\"", "name=\"lon\"", "name=\"radius\"",
                         "name=\"poll_interval\"", "name=\"client_id\"", "name=\"client_secret\"",
                         "name=\"language\"", "name=\"web_language\"", "name=\"auto_cycle\"",
                         "name=\"auto_cycle_interval\"", "name=\"near_airport_km\"",
                         "name=\"climb_rate_mps\"", "name=\"tab\"", "name=\"creds_file\""};
  for (const char *n : names) TEST_ASSERT_TRUE_MESSAGE(contains(h, n), n);
  TEST_ASSERT_TRUE(contains(h, "id=\"lat\" name=\"lat\" inputmode=\"decimal\" autocomplete=\"off\" value=\"52.22970\""));
}

static void test_only_the_active_tab_is_visible(void) {
  ConfigPageModel m = baseModel();
  m.tab = SettingsTab::OpenSky;
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<section data-tab=\"opensky\">"));
  TEST_ASSERT_TRUE(contains(h, "<section data-tab=\"location\" hidden>"));
  TEST_ASSERT_TRUE(contains(h, "<section data-tab=\"general\" hidden>"));
  TEST_ASSERT_TRUE(contains(h, "data-t=\"opensky\" class=\"on\""));
  TEST_ASSERT_TRUE(contains(h, "id=\"tab\" value=\"opensky\""));
}

static void test_web_language_switches_the_page_text(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.cfg.language = Language::PL;  // the display language doesn't matter here
  std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<html lang=\"en\">"));
  TEST_ASSERT_TRUE(contains(h, ">Location</button>"));
  TEST_ASSERT_TRUE(contains(h, "class=\"on\" href=\"/lang?l=en\""));

  m.cfg.web_language = Language::PL;
  h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<html lang=\"pl\">"));
  TEST_ASSERT_TRUE(contains(h, ">Lokalizacja</button>"));
  TEST_ASSERT_TRUE(contains(h, "class=\"on\" href=\"/lang?l=pl\""));
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
  TEST_ASSERT_TRUE(h.find("value=\"pl\" selected", web) != std::string::npos);  // web: PL
}

static void test_user_values_are_escaped(void) {
  ConfigPageModel m = baseModel();
  m.cfg.opensky_client_id = "a\"><script>x</script>";
  const std::string h = render(m);
  TEST_ASSERT_FALSE(contains(h, "<script>x</script>"));
  TEST_ASSERT_TRUE(contains(h, "a&quot;&gt;&lt;script&gt;"));
}

static void test_message_and_detail(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.hasMessage = true;
  m.message = WebStr::SavedWithInvalid;
  m.isError = true;
  m.messageDetail = "Latitude, <Radius>";
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<div class=\"msg err\">Saved, but"));
  TEST_ASSERT_TRUE(contains(h, "Latitude, &lt;Radius&gt;"));

  m.hasMessage = false;
  TEST_ASSERT_FALSE(contains(render(m), "class=\"msg"));
}

static void test_status_and_daily_credit_readout(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  std::string h = render(m);  // anonymous, 15 s polls: 5760 > 400
  TEST_ASSERT_TRUE(contains(h, "Anonymous mode"));
  TEST_ASSERT_TRUE(contains(h, "<b>5760</b> credits a day"));
  TEST_ASSERT_TRUE(contains(h, "limit 400<div class=\"warn\">"));

  m.cfg.opensky_client_id = "id";
  m.cfg.opensky_client_secret = "secret";
  m.cfg.poll_interval_s = 30;  // 2880 <= 4000
  h = render(m);
  TEST_ASSERT_TRUE(contains(h, "class=\"pill ok\""));
  TEST_ASSERT_TRUE(contains(h, "<b>2880</b>"));
  TEST_ASSERT_FALSE(contains(h, "<div class=\"warn\">"));
}

static void test_auto_cycle_checkbox_state(void) {
  ConfigPageModel m = baseModel();
  m.cfg.auto_cycle_enabled = true;
  TEST_ASSERT_TRUE(contains(render(m), "name=\"auto_cycle\" value=\"1\" checked"));
  m.cfg.auto_cycle_enabled = false;
  TEST_ASSERT_FALSE(contains(render(m), "name=\"auto_cycle\" value=\"1\" checked"));
}

static void test_footer_shows_the_firmware_version(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::PL;
  std::string h = render(m);
  // Native builds get no injected version -> the fallback.
  TEST_ASSERT_TRUE(contains(h, "wersja <span id=\"ver\">dev</span>"));
  m.cfg.web_language = Language::EN;
  TEST_ASSERT_TRUE(contains(render(m), "version <span id=\"ver\">dev</span>"));
}

static void test_backup_tab_has_download_and_restore(void) {
  ConfigPageModel m = baseModel();
  m.tab = SettingsTab::Backup;
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "data-t=\"backup\" class=\"on\""));
  TEST_ASSERT_TRUE(contains(h, "<section data-tab=\"backup\">"));
  // Download: GET form with the secrets checkbox (on by default).
  TEST_ASSERT_TRUE(contains(h, "<form id=\"bk\" method=\"GET\" action=\"/backup.json\">"));
  TEST_ASSERT_TRUE(contains(h, "name=\"secrets\" value=\"1\" form=\"bk\" checked"));
  // Restore: multipart upload in its own form.
  TEST_ASSERT_TRUE(contains(h, "<form id=\"rs\" method=\"POST\" action=\"/restore\" "
                               "enctype=\"multipart/form-data\">"));
  TEST_ASSERT_TRUE(contains(h, "name=\"backup_file\" form=\"rs\""));
  // The settings Save button means nothing on this tab.
  TEST_ASSERT_TRUE(contains(h, "id=\"save\" hidden>"));

  m.tab = SettingsTab::Location;
  TEST_ASSERT_FALSE(contains(render(m), "id=\"save\" hidden>"));
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
  TEST_ASSERT_TRUE(contains(h, "<div class=\"stat quota\" id=\"quota\">Credits left today: 3971"));

  m.quota.state = OpenSkyQuota::State::Exhausted;
  m.quota.reset_in_s = 7200;
  m.quotaResetInS = 3600;
  h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<div class=\"stat quota warn\" id=\"quota\">Daily limit reached: "
                               "renews in about 1 h."));
}

static void test_firmware_tab(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.tab = SettingsTab::Firmware;
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "<section data-tab=\"firmware\">"));
  TEST_ASSERT_TRUE(contains(h, "id=\"tab\" value=\"firmware\""));
  TEST_ASSERT_TRUE(contains(h, "<form id=\"fw\" method=\"POST\" action=\"/update\" "
                               "enctype=\"multipart/form-data\">"));
  TEST_ASSERT_TRUE(contains(h, "name=\"firmware\" form=\"fw\""));
  TEST_ASSERT_TRUE(contains(h, "Installed version: <b id=\"fwv\">dev</b>"));
  TEST_ASSERT_TRUE(contains(h, kReleasesUrl));
  TEST_ASSERT_TRUE(contains(h, "id=\"save\" hidden>"));  // nothing to save here
  TEST_ASSERT_TRUE(contains(h, ">Firmware</button>"));
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
  TEST_ASSERT_TRUE(contains(h, "<div class=\"card\" id=\"gh\"><h2>Updates from GitHub</h2>"));
  TEST_ASSERT_TRUE(contains(h, "Installed version: <b id=\"fwv\">v1.2.0</b>"));
  TEST_ASSERT_TRUE(contains(h, "Latest release: <b>v1.3.0</b> \xC2\xB7 published 20.10.2026"));
  TEST_ASSERT_TRUE(contains(h, "href=\"https://github.com/alasota/CYD-Flight-Tracker/releases/tag/v1.3.0\""));
  TEST_ASSERT_TRUE(contains(h, "<span class=\"pill ok\" id=\"ups\"><span class=\"dot\"></span>"
                               "A newer version is available.</span>"));
  TEST_ASSERT_TRUE(contains(h, "checked 24.10.2026 09:11"));
  TEST_ASSERT_TRUE(contains(h, "<button form=\"ui\" class=\"wide\" id=\"upi\""));
  TEST_ASSERT_TRUE(contains(h, ">Install v1.3.0</button>"));
  TEST_ASSERT_TRUE(contains(h, "<form id=\"ui\" method=\"POST\" action=\"/update/install\">"
                               "<input type=\"hidden\" name=\"tag\" value=\"v1.3.0\"></form>"));
  TEST_ASSERT_TRUE(contains(h, "<form id=\"uc\" method=\"POST\" action=\"/update/check\">"));
  TEST_ASSERT_TRUE(contains(h, ">Check now</button>"));
  TEST_ASSERT_TRUE(contains(h, "version <span id=\"ver\">v1.2.0</span>"));
  // Development versions: the switch is there, off, and nothing else.
  TEST_ASSERT_TRUE(contains(h, "name=\"on\" value=\"1\" form=\"upr\" "
                               "onchange=\"this.form.submit()\"></label>"));
  TEST_ASSERT_FALSE(contains(h, "id=\"upre\""));
  TEST_ASSERT_FALSE(contains(h, "id=\"upip\""));
}

static void test_update_card_install_button_only_when_installable(void) {
  ConfigPageModel m = updateModel(false);
  for (UpdateStatus st : {UpdateStatus::UpToDate, UpdateStatus::NoChecksum, UpdateStatus::NoAsset,
                          UpdateStatus::CheckFailed, UpdateStatus::RateLimited,
                          UpdateStatus::NeverChecked}) {
    m.update.status = st;
    const std::string h = render(m);
    TEST_ASSERT_FALSE(contains(h, "id=\"upi\""));
    TEST_ASSERT_TRUE(contains(h, webTr(updateStatusMessage(st), Language::EN)));
  }
  m.update.status = UpdateStatus::BackToRelease;
  TEST_ASSERT_TRUE(contains(render(m), ">Go back to release v1.3.0</button>"));
  m.update.status = UpdateStatus::DevBuild;
  TEST_ASSERT_TRUE(contains(render(m), ">Install v1.3.0</button>"));
  // The installed version is never offered again.
  m.installedVersion = "v1.3.0";
  TEST_ASSERT_FALSE(contains(render(m), "id=\"upi\""));
}

static void test_update_card_development_channel(void) {
  ConfigPageModel m = updateModel(true);
  std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "onchange=\"this.form.submit()\" checked></label>"));
  TEST_ASSERT_TRUE(contains(h, "id=\"upre\""));
  TEST_ASSERT_TRUE(contains(h, "Latest development version: <b>v1.4.0-rc.1</b>"));
  TEST_ASSERT_TRUE(contains(h, "A test version"));
  TEST_ASSERT_TRUE(contains(h, ">Install v1.4.0-rc.1</button>"));
  TEST_ASSERT_TRUE(contains(h, "<form id=\"uip\" method=\"POST\" action=\"/update/install\">"
                               "<input type=\"hidden\" name=\"tag\" value=\"v1.4.0-rc.1\">"));
  // Switch off: the pre-release isn't shown even if the state still has it.
  m.cfg.update_show_prerelease = false;
  h = render(m);
  TEST_ASSERT_FALSE(contains(h, "id=\"upre\""));
  TEST_ASSERT_FALSE(contains(h, ">Install v1.4.0-rc.1</button>"));
  // Already running it: no button, a note instead.
  m = updateModel(true);
  m.installedVersion = "v1.4.0-rc.1";
  h = render(m);
  TEST_ASSERT_FALSE(contains(h, "id=\"upip\""));
  TEST_ASSERT_TRUE(contains(h, "v1.4.0-rc.1 \xC2\xB7 installed"));
}

static void test_update_card_shows_a_failed_install(void) {
  ConfigPageModel m = updateModel(false);
  m.update.lastInstall = OtaResult::ChecksumMismatch;
  m.update.lastInstallTag = "v1.3.0";
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "The last install failed (v1.3.0): Not updated: the downloaded "
                               "file's SHA-256 does not match"));
}

static void test_update_card_escapes_what_came_from_the_network(void) {
  ConfigPageModel m = updateModel(true);
  m.update.latest.tag = "v1.3.0\"><script>";
  m.update.latestPre.tag = "<b>";
  const std::string h = render(m);
  TEST_ASSERT_FALSE(contains(h, "v1.3.0\"><script>"));
  TEST_ASSERT_TRUE(contains(h, "<b>v1.3.0&quot;&gt;&lt;script&gt;</b>"));
}

static void test_update_check_switch_on_general(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  TEST_ASSERT_TRUE(contains(render(m), "name=\"update_check\" value=\"1\" checked"));
  m.cfg.update_check_enabled = false;
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "name=\"update_check\" value=\"1\">"));
  TEST_ASSERT_FALSE(contains(h, "name=\"update_check\" value=\"1\" checked"));
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
  TEST_ASSERT_FALSE(contains(render(m), "location.href='/?tab=firmware'"));
  m.restarting = true;
  const std::string h = render(m);
  TEST_ASSERT_TRUE(contains(h, "Firmware updated: v1.2.0"));
  TEST_ASSERT_TRUE(contains(h, "The board is restarting"));
  TEST_ASSERT_TRUE(contains(h, "location.href='/?tab=firmware'"));
}

static void test_saved_secret_never_reaches_the_page(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  m.cfg.opensky_client_id = "my-id";
  m.cfg.opensky_client_secret = "TOP-SECRET-VALUE";
  std::string h = render(m);
  TEST_ASSERT_FALSE(contains(h, "TOP-SECRET-VALUE"));
  TEST_ASSERT_TRUE(contains(h, "name=\"client_secret\" autocomplete=\"off\" value=\"\" "
                               "data-saved=\"1\" placeholder=\"saved"));
  TEST_ASSERT_TRUE(contains(h, "name=\"clear_credentials\""));

  m.cfg.opensky_client_secret.clear();
  h = render(m);
  TEST_ASSERT_FALSE(contains(h, "data-saved"));
  TEST_ASSERT_FALSE(contains(h, "name=\"clear_credentials\""));
}

static void test_panel_password_card(void) {
  ConfigPageModel m = baseModel();
  m.cfg.web_language = Language::EN;
  std::string h = render(m);
  // No password: warned on General and on Firmware, nothing to remove.
  size_t first = h.find("The panel has no password");
  TEST_ASSERT_TRUE(first != std::string::npos);
  TEST_ASSERT_TRUE(h.find("The panel has no password", first + 1) != std::string::npos);
  TEST_ASSERT_TRUE(contains(h, "name=\"new_password\" autocomplete=\"new-password\""));
  TEST_ASSERT_TRUE(contains(h, "name=\"new_password2\""));
  TEST_ASSERT_FALSE(contains(h, "name=\"clear_password\""));

  m.cfg.web_password = "hunter22-secret";
  h = render(m);
  TEST_ASSERT_FALSE(contains(h, "hunter22-secret"));  // never echoed
  TEST_ASSERT_FALSE(contains(h, "The panel has no password"));
  TEST_ASSERT_TRUE(contains(h, "Password set"));
  TEST_ASSERT_TRUE(contains(h, "name=\"clear_password\""));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_quota_status_text);
  RUN_TEST(test_opensky_tab_shows_the_quota);
  RUN_TEST(test_backup_tab_has_download_and_restore);
  RUN_TEST(test_footer_shows_the_firmware_version);
  RUN_TEST(test_tab_ids_round_trip);
  RUN_TEST(test_daily_credits);
  RUN_TEST(test_daily_credits_rounding_and_zero_interval);
  RUN_TEST(test_daily_limits);
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
  return UNITY_END();
}
