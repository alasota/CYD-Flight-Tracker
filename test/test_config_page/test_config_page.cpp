#include <unity.h>

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
  for (SettingsTab t : {SettingsTab::Location, SettingsTab::OpenSky, SettingsTab::General}) {
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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_tab_ids_round_trip);
  RUN_TEST(test_daily_credits);
  RUN_TEST(test_daily_credits_rounding_and_zero_interval);
  RUN_TEST(test_daily_limits);
  RUN_TEST(test_credentials_need_both_fields);
  RUN_TEST(test_html_escape);
  RUN_TEST(test_page_is_complete_and_never_emits_empty_chunks);
  RUN_TEST(test_all_form_fields_present);
  RUN_TEST(test_only_the_active_tab_is_visible);
  RUN_TEST(test_web_language_switches_the_page_text);
  RUN_TEST(test_selects_reflect_both_languages_independently);
  RUN_TEST(test_user_values_are_escaped);
  RUN_TEST(test_message_and_detail);
  RUN_TEST(test_status_and_daily_credit_readout);
  RUN_TEST(test_auto_cycle_checkbox_state);
  return UNITY_END();
}
