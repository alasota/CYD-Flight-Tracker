// settings_form: credentials JSON, form number checks, the settings form (POST /save). CLAUDE.md "Code conventions" (settings_form), docs/architecture.md "Settings page".
#include <unity.h>

#include "admin/core/settings_form.h"

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "fixtures.h"
#include "test_support.h"

namespace {

static void test_parse_credentials_json_valid(void) {
  OpenSkyCredentials creds =
      parseOpenSkyCredentialsJson(R"({"clientId":"przykladowy id","clientSecret":"tajne haslo"})");

  TEST_ASSERT_TRUE(creds.ok);
  TEST_ASSERT_EQUAL_STRING("przykladowy id", creds.client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("tajne haslo", creds.client_secret.c_str());
}

static void test_parse_credentials_json_ignores_extra_fields_and_whitespace(void) {
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(
      "{\n  \"clientId\": \"abc123\",\n  \"clientSecret\": \"s3cr3t\",\n  \"issuedAt\": "
      "\"2026-01-01\"\n}");

  TEST_ASSERT_TRUE(creds.ok);
  TEST_ASSERT_EQUAL_STRING("abc123", creds.client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("s3cr3t", creds.client_secret.c_str());
}

static void test_parse_credentials_json_rejects_malformed_json(void) {
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson("not json at all");

  TEST_ASSERT_FALSE(creds.ok);
  TEST_ASSERT_TRUE(creds.client_id.empty());
  TEST_ASSERT_TRUE(creds.client_secret.empty());
}

static void test_parse_credentials_json_rejects_missing_field(void) {
  OpenSkyCredentials missingSecret = parseOpenSkyCredentialsJson(R"({"clientId":"abc123"})");
  TEST_ASSERT_FALSE(missingSecret.ok);

  OpenSkyCredentials missingId = parseOpenSkyCredentialsJson(R"({"clientSecret":"s3cr3t"})");
  TEST_ASSERT_FALSE(missingId.ok);
}

static void test_parse_credentials_json_rejects_empty_values(void) {
  OpenSkyCredentials creds =
      parseOpenSkyCredentialsJson(R"({"clientId":"","clientSecret":""})");

  TEST_ASSERT_FALSE(creds.ok);
}

static void test_parse_credentials_json_rejects_empty_string(void) {
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson("");

  TEST_ASSERT_FALSE(creds.ok);
}

static void test_parse_credentials_json_rejects_oversized_body(void) {
  // Padding well past kMaxCredentialsJsonBytes — the size check must
  // reject it before any parsing is attempted (docs/review-notes-2026.md 4.3).
  std::string oversized(kMaxCredentialsJsonBytes + 1, 'x');
  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(oversized);
  TEST_ASSERT_FALSE(creds.ok);
}

// ---- isValidFloatString (docs/review-notes-2026.md 1.6) --------------------

static void test_valid_float_string_accepts_typical_values(void) {
  TEST_ASSERT_TRUE(isValidFloatString("51.5074"));
  TEST_ASSERT_TRUE(isValidFloatString("-0.1278"));
  TEST_ASSERT_TRUE(isValidFloatString("10"));
  TEST_ASSERT_TRUE(isValidFloatString("0.1"));
  TEST_ASSERT_TRUE(isValidFloatString("  12.5"));  // leading whitespace tolerated, like strtof()
}

static void test_valid_float_string_rejects_garbage(void) {
  TEST_ASSERT_FALSE(isValidFloatString(""));
  TEST_ASSERT_FALSE(isValidFloatString("abc"));
  TEST_ASSERT_FALSE(isValidFloatString("12.3.4"));
  TEST_ASSERT_FALSE(isValidFloatString("12abc"));
  TEST_ASSERT_FALSE(isValidFloatString("12 34"));  // trailing garbage after the number
  TEST_ASSERT_FALSE(isValidFloatString(" "));
}

// ---- isValidUnsignedIntString (docs/review-notes-2026.md 1.6) --------------

static void test_valid_unsigned_int_string_accepts_digits(void) {
  TEST_ASSERT_TRUE(isValidUnsignedIntString("15"));
  TEST_ASSERT_TRUE(isValidUnsignedIntString("0"));
  TEST_ASSERT_TRUE(isValidUnsignedIntString("3600"));
}

static void test_valid_unsigned_int_string_rejects_non_digits(void) {
  TEST_ASSERT_FALSE(isValidUnsignedIntString(""));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("-5"));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("12.5"));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("abc"));
  TEST_ASSERT_FALSE(isValidUnsignedIntString("15s"));
}

// ---- applySettingsForm(): one POST /save (docs/review-notes-2026.md 1.5, 1.6) ----

static FormArgs form(std::initializer_list<std::pair<const char *, const char *>> fields) {
  FormArgs a;
  for (const auto &f : fields) a.add(f.first, f.second);
  return a;
}

// "[Latitude, Longitude]" in English, for messages.
static std::string labels(const std::vector<WebStr> &v) {
  std::string out = "[";
  for (size_t i = 0; i < v.size(); ++i) {
    if (i > 0) out += ", ";
    out += webTr(v[i], Language::EN);
  }
  return out + "]";
}

static void assertRejected(const std::vector<WebStr> &want, const std::vector<WebStr> &got) {
  ASSERT_STR_EQ(labels(want), labels(got));
}

// Saved settings that differ from every value the forms below send.
static Config savedConfig() {
  Config c = makeConfig();
  c.radius_deg = 2.0f;
  c.poll_interval_s = 60;
  c.auto_cycle_interval_s = 45;
  c.near_airport_km = 12.0f;
  c.climb_rate_threshold_mps = 1.0f;
  c.brightness_min_pct = 15;
  c.night_wake_s = 90;
  c.night_from_min = 23 * 60;
  c.night_to_min = 6 * 60;
  c.opensky_client_id = "old-id";
  c.opensky_client_secret = "old-secret";
  c.web_password = "stare-haslo";
  return c;
}

// Poprawny formularz ustawia wszystkie pola i oznacza dom jako ustawiony
static void test_form_valid_submit_sets_every_field_and_home_configured(void) {
  Config cfg = defaultConfig();
  TEST_ASSERT_FALSE(cfg.home_configured);
  const FormArgs args = form({{"lat", "50.0614"},
                              {"lon", "19.9366"},
                              {"radius", "1.5"},
                              {"poll_interval", "30"},
                              {"auto_cycle", "on"},
                              {"auto_cycle_interval", "20"},
                              {"near_airport_km", "10"},
                              {"climb_rate_mps", "2.5"},
                              {"update_check", "on"},
                              {"brightness_mode", "fixed"},
                              {"bl_min", "20"},
                              {"bl_max", "90"},
                              {"bl_fixed", "50"},
                              {"night_mode", "schedule"},
                              {"night_from", "22:30"},
                              {"night_to", "05:45"},
                              {"night_pct", "0"},
                              {"night_wake_s", "120"},
                              {"night_pause", "on"},
                              {"language", "en"},
                              {"web_language", "en"},
                              {"client_id", "my-id"},
                              {"client_secret", "my-secret"},
                              {"new_password", ""},
                              {"new_password2", ""}});
  const SettingsFormOutcome out = applySettingsForm(cfg, args);

  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0614f, cfg.home_lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 19.9366f, cfg.home_lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, cfg.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(30, cfg.poll_interval_s);
  TEST_ASSERT_TRUE(cfg.auto_cycle_enabled);
  TEST_ASSERT_EQUAL_UINT32(20, cfg.auto_cycle_interval_s);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, cfg.near_airport_km);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.5f, cfg.climb_rate_threshold_mps);
  TEST_ASSERT_TRUE(cfg.update_check_enabled);
  TEST_ASSERT_FALSE(cfg.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(20, cfg.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(90, cfg.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT8(50, cfg.brightness_fixed_pct);
  ASSERT_ENUM_EQ(NightMode::Schedule, cfg.night_mode);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 30, cfg.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(5 * 60 + 45, cfg.night_to_min);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.night_pct);
  TEST_ASSERT_EQUAL_UINT16(120, cfg.night_wake_s);
  TEST_ASSERT_TRUE(cfg.night_pause_polling);
  ASSERT_ENUM_EQ(Language::EN, cfg.language);
  ASSERT_ENUM_EQ(Language::EN, cfg.web_language);
  ASSERT_STR_EQ("my-id", cfg.opensky_client_id);
  ASSERT_STR_EQ("my-secret", cfg.opensky_client_secret);
  TEST_ASSERT_TRUE(cfg.home_configured);
  assertRejected({}, out.rejected);
  ASSERT_ENUM_EQ(WebStr::Saved, out.message);
}

// Błędna wartość pola zostaje odrzucona, a pole zachowuje poprzednią wartość
static void test_form_rejects_malformed_number_and_keeps_old_value(void) {
  struct Example {
    const char *field;
    const char *value;
    WebStr label;
    std::function<float(const Config &)> get;
  };
  const Example examples[] = {
      {"lat", "abc", WebStr::Latitude, [](const Config &c) { return c.home_lat; }},
      {"lon", "12.3.4", WebStr::Longitude, [](const Config &c) { return c.home_lon; }},
      {"radius", "", WebStr::Radius, [](const Config &c) { return c.radius_deg; }},
      {"poll_interval", "-5", WebStr::PollInterval,
       [](const Config &c) { return static_cast<float>(c.poll_interval_s); }},
      {"auto_cycle_interval", "10s", WebStr::AutoCycleInterval,
       [](const Config &c) { return static_cast<float>(c.auto_cycle_interval_s); }},
      {"near_airport_km", "1e", WebStr::NearAirport,
       [](const Config &c) { return c.near_airport_km; }},
      {"climb_rate_mps", "x", WebStr::ClimbRate,
       [](const Config &c) { return c.climb_rate_threshold_mps; }},
      {"bl_min", "12345", WebStr::BrightnessMin,
       [](const Config &c) { return static_cast<float>(c.brightness_min_pct); }},
      {"night_wake_s", "3.5", WebStr::NightWake,
       [](const Config &c) { return static_cast<float>(c.night_wake_s); }},
      {"night_from", "25:00", WebStr::NightFrom,
       [](const Config &c) { return static_cast<float>(c.night_from_min); }},
  };
  for (const Example &e : examples) {
    const std::string what = std::string(e.field) + "=\"" + e.value + "\"";
    const Config before = savedConfig();
    Config cfg = before;
    const SettingsFormOutcome out = applySettingsForm(cfg, form({{e.field, e.value}}));
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, e.get(before), e.get(cfg), what.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(labels({e.label}).c_str(), labels(out.rejected).c_str(),
                                     what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(WebStr::SavedWithInvalid),
                                  static_cast<int>(out.message), what.c_str());
  }
}

// Kilka błędnych pól jest wymienionych w kolejności formularza, w języku panelu
static void test_form_rejected_labels_in_form_order_in_panel_language(void) {
  Config cfg = savedConfig();
  // Sent in another order: the list follows the form (latitude, longitude, ..., password).
  const SettingsFormOutcome out = applySettingsForm(
      cfg, form({{"new_password", "nowe-haslo-1"},
                 {"new_password2", "inne-haslo-2"},
                 {"lon", "x"},
                 {"lat", "abc"}}));
  assertRejected({WebStr::Latitude, WebStr::Longitude, WebStr::PasswordMismatch}, out.rejected);
  ASSERT_STR_EQ(
      "Szerokość geograficzna, Długość geograficzna, hasło panelu (wpisane hasła się różnią)",
      rejectedFieldsText(out.rejected, Language::PL));
  ASSERT_STR_EQ("Latitude, Longitude, panel password (the two entries differ)",
                rejectedFieldsText(out.rejected, Language::EN));
}

// Brak pola wyboru znaczy „odznaczone”, a brak pola tekstowego „bez zmian”
static void test_form_missing_checkbox_unchecked_missing_text_unchanged(void) {
  Config cfg = savedConfig();
  cfg.auto_cycle_enabled = true;
  cfg.update_check_enabled = true;
  cfg.night_pause_polling = true;
  const float lat = cfg.home_lat;
  const SettingsFormOutcome out = applySettingsForm(cfg, form({{"lon", "21.5"}}));
  TEST_ASSERT_FALSE(cfg.auto_cycle_enabled);
  TEST_ASSERT_FALSE(cfg.update_check_enabled);
  TEST_ASSERT_FALSE(cfg.night_pause_polling);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, lat, cfg.home_lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 21.5f, cfg.home_lon);
  assertRejected({}, out.rejected);
}

// Puste pole sekretu OpenSky zachowuje zapisany sekret, a „wyczyść” usuwa id i sekret
static void test_form_empty_secret_keeps_saved_clear_removes_both(void) {
  Config cfg = savedConfig();
  applySettingsForm(cfg, form({{"client_id", "new-id"}, {"client_secret", ""}}));
  ASSERT_STR_EQ("new-id", cfg.opensky_client_id);
  ASSERT_STR_EQ("old-secret", cfg.opensky_client_secret);

  applySettingsForm(cfg, form({{"client_id", "new-id"},
                               {"client_secret", "typed-secret"},
                               {"clear_credentials", "1"}}));
  ASSERT_STR_EQ("", cfg.opensky_client_id);
  ASSERT_STR_EQ("", cfg.opensky_client_secret);
}

// Hasło panelu z formularza: komunikat i zmiana
static void test_form_password_outcomes_and_messages(void) {
  struct Example {
    const char *pw;
    const char *repeat;
    bool clear;
    const char *result;
    WebStr message;  // the page's message, or the rejected field when `rejected`
    bool rejected;
  };
  const Example examples[] = {
      {"", "", false, "stare-haslo", WebStr::Saved, false},
      {"nowe-haslo-1", "nowe-haslo-1", false, "nowe-haslo-1", WebStr::PasswordChanged, false},
      {"nowe-haslo-1", "inne-haslo-2", false, "stare-haslo", WebStr::PasswordMismatch, true},
      {"krotkie", "krotkie", false, "stare-haslo", WebStr::PasswordTooShort, true},
      {"nowe-haslo-1", "nowe-haslo-1", true, "", WebStr::PasswordCleared, false},
  };
  for (const Example &e : examples) {
    const std::string what = std::string("new_password=\"") + e.pw + "\", new_password2=\"" +
                             e.repeat + "\", clear_password " + (e.clear ? "yes" : "no");
    Config cfg = savedConfig();
    FormArgs args = form({{"new_password", e.pw}, {"new_password2", e.repeat}});
    if (e.clear) args.add("clear_password", "1");
    const SettingsFormOutcome out = applySettingsForm(cfg, args);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(e.result, cfg.web_password.c_str(), what.c_str());
    if (e.rejected) {
      TEST_ASSERT_EQUAL_STRING_MESSAGE(labels({e.message}).c_str(), labels(out.rejected).c_str(),
                                       what.c_str());
      TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(WebStr::SavedWithInvalid),
                                    static_cast<int>(out.message), what.c_str());
    } else {
      TEST_ASSERT_EQUAL_STRING_MESSAGE("[]", labels(out.rejected).c_str(), what.c_str());
      TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.message), static_cast<int>(out.message),
                                    what.c_str());
    }
  }
}

// Liczby jasności i nocy poza zakresem są przycinane, a nie odrzucane
static void test_form_brightness_out_of_range_is_clamped(void) {
  Config cfg = savedConfig();
  const SettingsFormOutcome out = applySettingsForm(
      cfg,
      form({{"bl_max", "150"}, {"bl_min", "0"}, {"night_pct", "101"}, {"night_wake_s", "5"}}));
  TEST_ASSERT_EQUAL_UINT8(100, cfg.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(100, cfg.night_pct);
  TEST_ASSERT_EQUAL_UINT16(10, cfg.night_wake_s);
  assertRejected({}, out.rejected);
}

// Pole wysłane dwa razy liczy się w pierwszej wersji, jak w WebServer::arg()
static void test_form_args_first_value_wins(void) {
  FormArgs args;
  args.add("lat", "52.1");
  args.add("lat", "abc");
  ASSERT_STR_EQ("52.1", args.get("lat"));
  Config cfg = savedConfig();
  const SettingsFormOutcome out = applySettingsForm(cfg, args);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 52.1f, cfg.home_lat);
  assertRejected({}, out.rejected);
}

// -- edge case: an absent field reads "" and isn't there; an empty one is there
static void test_form_args_absent_value_is_empty_string(void) {
  FormArgs args;
  args.add("radius", "");
  TEST_ASSERT_TRUE(args.has("radius"));
  ASSERT_STR_EQ("", args.get("radius"));
  TEST_ASSERT_FALSE(args.has("lat"));
  ASSERT_STR_EQ("", args.get("lat"));
}

// -- edge case: a language code the page doesn't know falls back to the default
static void test_form_unknown_language_code_falls_back_to_default(void) {
  Config cfg = savedConfig();
  cfg.language = Language::EN;
  cfg.web_language = Language::EN;
  applySettingsForm(cfg, form({{"language", "xx"}, {"web_language", "de"}}));
  ASSERT_ENUM_EQ(kDefaultLanguage, cfg.language);
  ASSERT_ENUM_EQ(kDefaultLanguage, cfg.web_language);
  cfg.language = Language::EN;
  applySettingsForm(cfg, form({{"lat", "52.0"}}));  // no language field: unchanged
  ASSERT_ENUM_EQ(Language::EN, cfg.language);
}

// -- edge case: brightness_mode "fixed" turns the sensor off, any other value on, none keeps it
static void test_form_brightness_mode_fixed_only_on_fixed(void) {
  Config cfg = savedConfig();
  cfg.brightness_auto = true;
  applySettingsForm(cfg, form({{"brightness_mode", "fixed"}}));
  TEST_ASSERT_FALSE(cfg.brightness_auto);
  applySettingsForm(cfg, form({{"lat", "52.0"}}));
  TEST_ASSERT_FALSE(cfg.brightness_auto);
  applySettingsForm(cfg, form({{"brightness_mode", "auto"}}));
  TEST_ASSERT_TRUE(cfg.brightness_auto);
  cfg.brightness_auto = false;
  applySettingsForm(cfg, form({{"brightness_mode", "whatever"}}));
  TEST_ASSERT_TRUE(cfg.brightness_auto);
}

// -- edge case: an unknown night mode code keeps the saved mode, without a rejected field
static void test_form_night_mode_unknown_code_keeps_mode(void) {
  Config cfg = savedConfig();
  cfg.night_mode = NightMode::Sun;
  const SettingsFormOutcome out = applySettingsForm(cfg, form({{"night_mode", "always"}}));
  ASSERT_ENUM_EQ(NightMode::Sun, cfg.night_mode);
  assertRejected({}, out.rejected);
  applySettingsForm(cfg, form({{"night_mode", "off"}}));
  ASSERT_ENUM_EQ(NightMode::Off, cfg.night_mode);
}

}  // namespace

void run_settings_form_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_parse_credentials_json_valid);
  RUN_TEST(test_parse_credentials_json_ignores_extra_fields_and_whitespace);
  RUN_TEST(test_parse_credentials_json_rejects_malformed_json);
  RUN_TEST(test_parse_credentials_json_rejects_missing_field);
  RUN_TEST(test_parse_credentials_json_rejects_empty_values);
  RUN_TEST(test_parse_credentials_json_rejects_empty_string);
  RUN_TEST(test_parse_credentials_json_rejects_oversized_body);

  RUN_TEST(test_valid_float_string_accepts_typical_values);
  RUN_TEST(test_valid_float_string_rejects_garbage);
  RUN_TEST(test_valid_unsigned_int_string_accepts_digits);
  RUN_TEST(test_valid_unsigned_int_string_rejects_non_digits);

  RUN_TEST(test_form_valid_submit_sets_every_field_and_home_configured);
  RUN_TEST(test_form_rejects_malformed_number_and_keeps_old_value);
  RUN_TEST(test_form_rejected_labels_in_form_order_in_panel_language);
  RUN_TEST(test_form_missing_checkbox_unchecked_missing_text_unchanged);
  RUN_TEST(test_form_empty_secret_keeps_saved_clear_removes_both);
  RUN_TEST(test_form_password_outcomes_and_messages);
  RUN_TEST(test_form_brightness_out_of_range_is_clamped);
  RUN_TEST(test_form_args_first_value_wins);
  RUN_TEST(test_form_args_absent_value_is_empty_string);
  RUN_TEST(test_form_unknown_language_code_falls_back_to_default);
  RUN_TEST(test_form_brightness_mode_fixed_only_on_fixed);
  RUN_TEST(test_form_night_mode_unknown_code_keeps_mode);
}
