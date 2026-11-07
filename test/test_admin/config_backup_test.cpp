// config_backup: settings backup / restore JSON. CLAUDE.md "Code conventions" (Backup / restore).
#include <unity.h>

#include <string>

#include "admin/core/config_backup.h"
#include "fixtures.h"
#include "json_assert.h"
#include "test_support.h"

namespace {

Settings sample() {
  Settings c = makeConfig();  // Warsaw, home_configured
  c.home.radius_deg = 1.5f;
  c.poll.interval_s = 30;
  c.opensky.client_id = "my-client";
  c.opensky.client_secret = "s3cr\"et";  // a quote: must survive JSON escaping
  c.display.language = Language::EN;
  c.web.language = Language::PL;
  c.auto_cycle.enabled = true;
  c.auto_cycle.interval_s = 20;
  c.phase.near_airport_km = 12.5f;
  c.phase.climb_rate_threshold_mps = 2.0f;
  c.update.check_enabled = false;
  c.update.show_prerelease = true;
  c.update.skipped_tag = "v1.4.0";  // device state: never in a backup
  return c;
}

static void test_round_trip_restores_every_setting(void) {
  const Settings original = sample();
  const std::string json = configToBackupJson(original, true, "v1.2.3");
  BackupRestoreResult r = restoreConfigFromBackupJson(json, defaultConfig());

  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_INT(24, r.appliedFields);  // 14 + the display group (10)
  const Settings &c = r.config;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, original.home.lat, c.home.lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, original.home.lon, c.home.lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, c.home.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(30u, c.poll.interval_s);
  TEST_ASSERT_EQUAL_STRING("my-client", c.opensky.client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("s3cr\"et", c.opensky.client_secret.c_str());
  ASSERT_ENUM_EQ(Language::EN, c.display.language);
  ASSERT_ENUM_EQ(Language::PL, c.web.language);
  TEST_ASSERT_TRUE(c.auto_cycle.enabled);
  TEST_ASSERT_EQUAL_UINT32(20u, c.auto_cycle.interval_s);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.5f, c.phase.near_airport_km);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, c.phase.climb_rate_threshold_mps);
  TEST_ASSERT_TRUE(c.home.configured);
  TEST_ASSERT_FALSE(c.update.check_enabled);
  TEST_ASSERT_TRUE(c.update.show_prerelease);
  TEST_ASSERT_TRUE(c.update.skipped_tag.empty());
}

static void test_update_settings_in_the_general_group(void) {
  const std::string json = configToBackupJson(sample(), false, "v1");
  const JsonDocument doc = parseJson(json);
  assertJsonBool(doc, "general.update_check", false);
  assertJsonBool(doc, "general.update_show_prerelease", true);
  ASSERT_NOT_CONTAINS(json, "v1.4.0");
  // An older backup without the keys keeps the current values.
  Settings current = defaultConfig();
  current.update.check_enabled = false;
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"general\":{}}", current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_FALSE(r.config.update.check_enabled);
  // A wrong type is skipped and listed.
  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,"
      "\"general\":{\"update_check\":\"yes\"}}",
      current);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("general.update_check", r.invalidFields[0].c_str());
}

static void test_backup_names_its_format_and_firmware(void) {
  const JsonDocument doc = parseJson(configToBackupJson(sample(), true, "v1.2.3"));
  assertJsonString(doc, "format", "cyd-sky-tracker-config");
  assertJsonInt(doc, "format_version", 1);
  assertJsonString(doc, "firmware", "v1.2.3");
  assertJsonString(doc, "general.language", "en");
  assertJsonString(doc, "opensky.client_id", "my-client");
  assertJsonString(doc, "opensky.client_secret", "s3cr\"et");
}

static void test_backup_without_secrets_keeps_the_devices_credentials(void) {
  const std::string json = configToBackupJson(sample(), false, "v1");
  const JsonDocument doc = parseJson(json);
  assertJsonMissing(doc, "opensky.client_id");
  assertJsonMissing(doc, "opensky.client_secret");
  assertJsonInt(doc, "opensky.poll_interval_s", 30);
  ASSERT_NOT_CONTAINS(json, "s3cr");  // nowhere else either

  Settings device = defaultConfig();
  device.opensky.client_id = "device-id";
  device.opensky.client_secret = "device-secret";
  BackupRestoreResult r = restoreConfigFromBackupJson(json, device);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_STRING("device-id", r.config.opensky.client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("device-secret", r.config.opensky.client_secret.c_str());
  TEST_ASSERT_EQUAL_UINT32(30u, r.config.poll.interval_s);  // the rest still restored
}

static void test_missing_keys_keep_current_values(void) {
  Settings current = sample();
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,"
      "\"general\":{\"auto_cycle\":false}}",
      current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(1, r.appliedFields);
  TEST_ASSERT_FALSE(r.config.auto_cycle.enabled);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, current.home.lat, r.config.home.lat);
  TEST_ASSERT_EQUAL_STRING("my-client", r.config.opensky.client_id.c_str());
}

static void test_wrong_types_are_skipped_and_reported(void) {
  Settings current = sample();
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,"
      "\"location\":{\"lat\":\"north\",\"lon\":20.5},"
      "\"opensky\":{\"poll_interval_s\":-5},"
      "\"general\":{\"language\":\"de\",\"auto_cycle\":\"yes\"}}",
      current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(1, r.appliedFields);  // only location.lon
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.5f, r.config.home.lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, current.home.lat, r.config.home.lat);  // kept
  TEST_ASSERT_EQUAL_UINT32(30u, r.config.poll.interval_s);                // kept
  ASSERT_ENUM_EQ(Language::EN, r.config.display.language);                    // kept
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("location.lat", r.invalidFields[0].c_str());
  TEST_ASSERT_EQUAL_STRING("opensky.poll_interval_s", r.invalidFields[1].c_str());
  TEST_ASSERT_EQUAL_STRING("general.language", r.invalidFields[2].c_str());
  TEST_ASSERT_EQUAL_STRING("general.auto_cycle", r.invalidFields[3].c_str());
}

static void test_restoring_location_marks_home_configured(void) {
  Settings fresh = defaultConfig();
  TEST_ASSERT_FALSE(fresh.home.configured);
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"location\":{\"lat\":50.0,\"lon\":19.9}}", fresh);
  TEST_ASSERT_TRUE(r.config.home.configured);

  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"general\":{\"auto_cycle\":true}}", fresh);
  TEST_ASSERT_FALSE(r.config.home.configured);
}

static void test_rejects_files_that_are_not_backups(void) {
  const Settings current = sample();
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson("", current).status);
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson("not json", current).status);
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson("[1,2]", current).status);
  // OpenSky's credentials file is JSON too, but not a backup.
  ASSERT_ENUM_EQ(BackupRestoreStatus::WrongFormat, restoreConfigFromBackupJson("{\"clientId\":\"a\",\"clientSecret\":\"b\"}", current) .status);
  // A newer format this firmware doesn't know.
  ASSERT_ENUM_EQ(BackupRestoreStatus::WrongFormat, restoreConfigFromBackupJson("{\"format\":\"cyd-sky-tracker-config\",\"format_version\":2}", current) .status);
  // A rejected file leaves the config untouched.
  BackupRestoreResult r = restoreConfigFromBackupJson("{\"format\":\"other\"}", current);
  TEST_ASSERT_EQUAL_STRING("my-client", r.config.opensky.client_id.c_str());
}

static void test_rejects_oversized_files(void) {
  std::string big = "{\"format\":\"cyd-sky-tracker-config\",\"pad\":\"";
  big += std::string(kMaxBackupJsonBytes, 'x');
  big += "\"}";
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson(big, sample()).status);
}

static void test_panel_password_travels_only_with_secrets(void) {
  Settings c = sample();
  c.web.password = "hunter22";
  assertJsonString(parseJson(configToBackupJson(c, true, "v1")), "security.web_password", "hunter22");
  ASSERT_NOT_CONTAINS(configToBackupJson(c, false, "v1"), "hunter22");
  assertJsonMissing(parseJson(configToBackupJson(c, false, "v1")), "security");
  assertJsonMissing(parseJson(configToBackupJson(sample(), true, "v1")), "security");  // none set

  BackupRestoreResult r = restoreConfigFromBackupJson(configToBackupJson(c, true, "v1"),
                                                      defaultConfig());
  TEST_ASSERT_EQUAL_STRING("hunter22", r.config.web.password.c_str());
  TEST_ASSERT_EQUAL_INT(25, r.appliedFields);
}

static void test_restore_without_password_keeps_the_devices(void) {
  Settings device = defaultConfig();
  device.web.password = "device-pass";
  BackupRestoreResult r = restoreConfigFromBackupJson(configToBackupJson(sample(), false, "v1"),
                                                      device);
  TEST_ASSERT_EQUAL_STRING("device-pass", r.config.web.password.c_str());
}

static void test_invalid_password_in_backup_is_reported_and_skipped(void) {
  Settings device = defaultConfig();
  device.web.password = "device-pass";
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"security\":{\"web_password\":\"short\"}}",
      device);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_STRING("device-pass", r.config.web.password.c_str());
  TEST_ASSERT_EQUAL_INT(0, r.appliedFields);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("security.web_password", r.invalidFields[0].c_str());
}

static void test_display_group_round_trip(void) {
  Settings c = defaultConfig();
  c.backlight.brightness_auto = false;
  c.backlight.brightness_fixed_pct = 42;
  c.backlight.brightness_min_pct = 15;
  c.backlight.brightness_max_pct = 90;
  c.backlight.night_mode = NightMode::Schedule;
  c.backlight.night_from_min = 22 * 60 + 15;
  c.backlight.night_to_min = 7 * 60;
  c.backlight.night_pct = 0;
  c.backlight.night_wake_s = 120;
  c.backlight.night_pause_polling = true;
  const std::string json = configToBackupJson(c, false, "v1");
  const JsonDocument doc = parseJson(json);
  assertJsonBool(doc, "display.brightness_auto", false);
  assertJsonInt(doc, "display.brightness_fixed", 42);
  assertJsonString(doc, "display.night_mode", "schedule");
  assertJsonString(doc, "display.night_from", "22:15");
  assertJsonString(doc, "display.night_to", "07:00");
  assertJsonInt(doc, "display.night_brightness", 0);
  assertJsonBool(doc, "display.night_pause_polling", true);
  BackupRestoreResult r = restoreConfigFromBackupJson(json, defaultConfig());
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_TRUE(r.invalidFields.empty());
  TEST_ASSERT_FALSE(r.config.backlight.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(42, r.config.backlight.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(15, r.config.backlight.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(90, r.config.backlight.brightness_max_pct);
  ASSERT_ENUM_EQ(NightMode::Schedule, r.config.backlight.night_mode);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 15, r.config.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(7 * 60, r.config.backlight.night_to_min);
  TEST_ASSERT_EQUAL_UINT8(0, r.config.backlight.night_pct);
  TEST_ASSERT_EQUAL_UINT16(120, r.config.backlight.night_wake_s);
  TEST_ASSERT_TRUE(r.config.backlight.night_pause_polling);
}

static void test_display_group_bad_values_and_old_backups(void) {
  // Without the group (a backup from before spec/03): current values kept.
  Settings current = defaultConfig();
  current.backlight.night_pct = 33;
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"general\":{}}", current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT8(33, r.config.backlight.night_pct);
  // Wrong values are skipped and listed; a huge number is capped (sanitizeConfig clamps it).
  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"display\":{"
      "\"night_mode\":\"dark\",\"night_from\":\"25:00\",\"night_brightness\":\"x\","
      "\"brightness_max\":70000}}",
      current);
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("display.night_mode", r.invalidFields[0].c_str());
  TEST_ASSERT_EQUAL_UINT8(255, r.config.backlight.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT8(33, r.config.backlight.night_pct);
  // An unknown group (a backup from a newer firmware) doesn't reject the file.
  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"future\":{\"x\":1}}",
      current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
}

// ---- Spec 12 stage 6: the backup must not change when Settings is rebuilt from context objects --

// A non-default value in every setting that goes into a backup (25 of them), plus what never
// does: the skipped release (device state) and the last screen.
Settings everyFieldChanged() {
  Settings c = makeConfig();  // Warsaw, home_configured
  c.home.radius_deg = 1.5f;
  c.poll.interval_s = 30;
  c.opensky.client_id = "my-client";
  c.opensky.client_secret = "s3cr\"et";
  c.display.language = Language::EN;
  c.web.language = Language::PL;  // differs from the display's: a key swap shows
  c.auto_cycle.enabled = true;
  c.auto_cycle.interval_s = 20;
  c.phase.near_airport_km = 12.5f;
  c.phase.climb_rate_threshold_mps = 2.25f;
  c.update.check_enabled = false;
  c.update.show_prerelease = true;
  c.backlight.brightness_auto = false;
  c.backlight.brightness_fixed_pct = 42;
  c.backlight.brightness_min_pct = 15;
  c.backlight.brightness_max_pct = 90;
  c.backlight.night_mode = NightMode::Schedule;
  c.backlight.night_from_min = 22 * 60 + 15;
  c.backlight.night_to_min = 7 * 60;
  c.backlight.night_pct = 5;
  c.backlight.night_wake_s = 120;
  c.backlight.night_pause_polling = true;
  c.web.password = "hunter22";
  c.update.skipped_tag = "v1.4.0";  // device state: never in a backup
  c.display.last_screen = 2;                // nav state: never in a backup
  return c;
}

// What v1.11.3 (c0a10e3) wrote for everyFieldChanged(), captured from configToBackupJson()
// before stage 6 — the groups, key names, order and pretty-printing a restore depends on.
// serializeJsonPretty() ends lines with CRLF; the raw strings below are compared through
// crlf(), so the checkout's line endings don't matter.
const char kV1113BackupWithSecrets[] = R"({
  "format": "cyd-sky-tracker-config",
  "format_version": 1,
  "firmware": "v1.11.3",
  "location": {
    "lat": 52.2297,
    "lon": 21.0122,
    "radius_deg": 1.5
  },
  "opensky": {
    "client_id": "my-client",
    "client_secret": "s3cr\"et",
    "poll_interval_s": 30
  },
  "general": {
    "language": "en",
    "web_language": "pl",
    "auto_cycle": true,
    "auto_cycle_interval_s": 20,
    "near_airport_km": 12.5,
    "climb_rate_mps": 2.25,
    "update_check": false,
    "update_show_prerelease": true
  },
  "display": {
    "brightness_auto": false,
    "brightness_fixed": 42,
    "brightness_min": 15,
    "brightness_max": 90,
    "night_mode": "schedule",
    "night_from": "22:15",
    "night_to": "07:00",
    "night_brightness": 5,
    "night_wake_s": 120,
    "night_pause_polling": true
  },
  "security": {
    "web_password": "hunter22"
  }
})";

const char kV1113BackupWithoutSecrets[] = R"({
  "format": "cyd-sky-tracker-config",
  "format_version": 1,
  "firmware": "v1.11.3",
  "location": {
    "lat": 52.2297,
    "lon": 21.0122,
    "radius_deg": 1.5
  },
  "opensky": {
    "poll_interval_s": 30
  },
  "general": {
    "language": "en",
    "web_language": "pl",
    "auto_cycle": true,
    "auto_cycle_interval_s": 20,
    "near_airport_km": 12.5,
    "climb_rate_mps": 2.25,
    "update_check": false,
    "update_show_prerelease": true
  },
  "display": {
    "brightness_auto": false,
    "brightness_fixed": 42,
    "brightness_min": 15,
    "brightness_max": 90,
    "night_mode": "schedule",
    "night_from": "22:15",
    "night_to": "07:00",
    "night_brightness": 5,
    "night_wake_s": 120,
    "night_pause_polling": true
  }
})";

// `text` with every line break as CRLF.
std::string crlf(const char *text) {
  std::string out;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p == '\r') continue;
    if (*p == '\n') out += '\r';
    out += *p;
  }
  return out;
}

// Scenario: Kopia zapasowa ma bajt w bajt ten sam JSON co przed etapem
static void test_backup_json_is_the_same_as_before_stage_6(void) {
  const Settings c = everyFieldChanged();
  const std::string withSecrets = configToBackupJson(c, true, "v1.11.3");
  const std::string withoutSecrets = configToBackupJson(c, false, "v1.11.3");
  ASSERT_STR_EQ(crlf(kV1113BackupWithSecrets), withSecrets);
  ASSERT_STR_EQ(crlf(kV1113BackupWithoutSecrets), withoutSecrets);
  ASSERT_NOT_CONTAINS(withSecrets, "v1.4.0");  // the skipped release (updskip) never travels
}

// Scenario: Kopia zapasowa zrobiona przed etapem 6 odtwarza każde ustawienie
static void test_restore_of_a_stage_5_backup_fills_every_setting(void) {
  // The page language starts as EN, so the backup's "pl" has to be applied, not left over.
  Settings base = defaultConfig();
  base.web.language = Language::EN;
  const BackupRestoreResult r = restoreConfigFromBackupJson(crlf(kV1113BackupWithSecrets), base);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_INT(25, r.appliedFields);
  const Settings &c = r.config;
  // location
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 52.2297f, c.home.lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 21.0122f, c.home.lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, c.home.radius_deg);
  TEST_ASSERT_TRUE(c.home.configured);
  // opensky
  ASSERT_STR_EQ("my-client", c.opensky.client_id);
  ASSERT_STR_EQ("s3cr\"et", c.opensky.client_secret);
  TEST_ASSERT_EQUAL_UINT32(30u, c.poll.interval_s);
  // general
  ASSERT_ENUM_EQ(Language::EN, c.display.language);
  ASSERT_ENUM_EQ(Language::PL, c.web.language);
  TEST_ASSERT_TRUE(c.auto_cycle.enabled);
  TEST_ASSERT_EQUAL_UINT32(20u, c.auto_cycle.interval_s);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.5f, c.phase.near_airport_km);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.25f, c.phase.climb_rate_threshold_mps);
  TEST_ASSERT_FALSE(c.update.check_enabled);
  TEST_ASSERT_TRUE(c.update.show_prerelease);
  // display
  TEST_ASSERT_FALSE(c.backlight.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(42, c.backlight.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(15, c.backlight.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(90, c.backlight.brightness_max_pct);
  ASSERT_ENUM_EQ(NightMode::Schedule, c.backlight.night_mode);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 15, c.backlight.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(7 * 60, c.backlight.night_to_min);
  TEST_ASSERT_EQUAL_UINT8(5, c.backlight.night_pct);
  TEST_ASSERT_EQUAL_UINT16(120, c.backlight.night_wake_s);
  TEST_ASSERT_TRUE(c.backlight.night_pause_polling);
  // security
  ASSERT_STR_EQ("hunter22", c.web.password);
  // Not in a backup: left as they were.
  TEST_ASSERT_TRUE(c.update.skipped_tag.empty());
  TEST_ASSERT_EQUAL_INT(0, c.display.last_screen);

  // The other way round (display "pl", page "en") lands in the other objects too.
  std::string json = crlf(kV1113BackupWithSecrets);
  const std::string displayEn = "\"language\": \"en\"";  // not "web_language": its '"' differs
  json.replace(json.find(displayEn), displayEn.size(), "\"language\": \"pl\"");
  const std::string webPl = "\"web_language\": \"pl\"";
  json.replace(json.find(webPl), webPl.size(), "\"web_language\": \"en\"");
  const BackupRestoreResult r2 = restoreConfigFromBackupJson(json, defaultConfig());
  ASSERT_ENUM_EQ(Language::PL, r2.config.display.language);
  ASSERT_ENUM_EQ(Language::EN, r2.config.web.language);
}

}  // namespace

void run_config_backup_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_round_trip_restores_every_setting);
  RUN_TEST(test_update_settings_in_the_general_group);
  RUN_TEST(test_display_group_round_trip);
  RUN_TEST(test_display_group_bad_values_and_old_backups);
  RUN_TEST(test_backup_names_its_format_and_firmware);
  RUN_TEST(test_backup_without_secrets_keeps_the_devices_credentials);
  RUN_TEST(test_missing_keys_keep_current_values);
  RUN_TEST(test_wrong_types_are_skipped_and_reported);
  RUN_TEST(test_restoring_location_marks_home_configured);
  RUN_TEST(test_rejects_files_that_are_not_backups);
  RUN_TEST(test_rejects_oversized_files);
  RUN_TEST(test_panel_password_travels_only_with_secrets);
  RUN_TEST(test_restore_without_password_keeps_the_devices);
  RUN_TEST(test_invalid_password_in_backup_is_reported_and_skipped);
  RUN_TEST(test_backup_json_is_the_same_as_before_stage_6);
  RUN_TEST(test_restore_of_a_stage_5_backup_fills_every_setting);
}
