// config_backup: settings backup / restore JSON. CLAUDE.md "Code conventions" (Backup / restore).
#include <unity.h>

#include <string>

#include "config_backup.h"
#include "fixtures.h"
#include "json_assert.h"
#include "test_support.h"

namespace {

Config sample() {
  Config c = makeConfig();  // Warsaw, home_configured
  c.radius_deg = 1.5f;
  c.poll_interval_s = 30;
  c.opensky_client_id = "my-client";
  c.opensky_client_secret = "s3cr\"et";  // a quote: must survive JSON escaping
  c.language = Language::EN;
  c.web_language = Language::PL;
  c.auto_cycle_enabled = true;
  c.auto_cycle_interval_s = 20;
  c.near_airport_km = 12.5f;
  c.climb_rate_threshold_mps = 2.0f;
  c.update_check_enabled = false;
  c.update_show_prerelease = true;
  c.update_skipped_tag = "v1.4.0";  // device state: never in a backup
  return c;
}

static void test_round_trip_restores_every_setting(void) {
  const Config original = sample();
  const std::string json = configToBackupJson(original, true, "v1.2.3");
  BackupRestoreResult r = restoreConfigFromBackupJson(json, defaultConfig());

  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_INT(24, r.appliedFields);  // 14 + the display group (10)
  const Config &c = r.config;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, original.home_lat, c.home_lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, original.home_lon, c.home_lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, c.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(30u, c.poll_interval_s);
  TEST_ASSERT_EQUAL_STRING("my-client", c.opensky_client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("s3cr\"et", c.opensky_client_secret.c_str());
  ASSERT_ENUM_EQ(Language::EN, c.language);
  ASSERT_ENUM_EQ(Language::PL, c.web_language);
  TEST_ASSERT_TRUE(c.auto_cycle_enabled);
  TEST_ASSERT_EQUAL_UINT32(20u, c.auto_cycle_interval_s);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.5f, c.near_airport_km);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, c.climb_rate_threshold_mps);
  TEST_ASSERT_TRUE(c.home_configured);
  TEST_ASSERT_FALSE(c.update_check_enabled);
  TEST_ASSERT_TRUE(c.update_show_prerelease);
  TEST_ASSERT_TRUE(c.update_skipped_tag.empty());
}

static void test_update_settings_in_the_general_group(void) {
  const std::string json = configToBackupJson(sample(), false, "v1");
  const JsonDocument doc = parseJson(json);
  assertJsonBool(doc, "general.update_check", false);
  assertJsonBool(doc, "general.update_show_prerelease", true);
  ASSERT_NOT_CONTAINS(json, "v1.4.0");
  // An older backup without the keys keeps the current values.
  Config current = defaultConfig();
  current.update_check_enabled = false;
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"general\":{}}", current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_FALSE(r.config.update_check_enabled);
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

  Config device = defaultConfig();
  device.opensky_client_id = "device-id";
  device.opensky_client_secret = "device-secret";
  BackupRestoreResult r = restoreConfigFromBackupJson(json, device);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_STRING("device-id", r.config.opensky_client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("device-secret", r.config.opensky_client_secret.c_str());
  TEST_ASSERT_EQUAL_UINT32(30u, r.config.poll_interval_s);  // the rest still restored
}

static void test_missing_keys_keep_current_values(void) {
  Config current = sample();
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,"
      "\"general\":{\"auto_cycle\":false}}",
      current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(1, r.appliedFields);
  TEST_ASSERT_FALSE(r.config.auto_cycle_enabled);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, current.home_lat, r.config.home_lat);
  TEST_ASSERT_EQUAL_STRING("my-client", r.config.opensky_client_id.c_str());
}

static void test_wrong_types_are_skipped_and_reported(void) {
  Config current = sample();
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,"
      "\"location\":{\"lat\":\"north\",\"lon\":20.5},"
      "\"opensky\":{\"poll_interval_s\":-5},"
      "\"general\":{\"language\":\"de\",\"auto_cycle\":\"yes\"}}",
      current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_INT(1, r.appliedFields);  // only location.lon
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.5f, r.config.home_lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, current.home_lat, r.config.home_lat);  // kept
  TEST_ASSERT_EQUAL_UINT32(30u, r.config.poll_interval_s);                // kept
  ASSERT_ENUM_EQ(Language::EN, r.config.language);                    // kept
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("location.lat", r.invalidFields[0].c_str());
  TEST_ASSERT_EQUAL_STRING("opensky.poll_interval_s", r.invalidFields[1].c_str());
  TEST_ASSERT_EQUAL_STRING("general.language", r.invalidFields[2].c_str());
  TEST_ASSERT_EQUAL_STRING("general.auto_cycle", r.invalidFields[3].c_str());
}

static void test_restoring_location_marks_home_configured(void) {
  Config fresh = defaultConfig();
  TEST_ASSERT_FALSE(fresh.home_configured);
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"location\":{\"lat\":50.0,\"lon\":19.9}}", fresh);
  TEST_ASSERT_TRUE(r.config.home_configured);

  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"general\":{\"auto_cycle\":true}}", fresh);
  TEST_ASSERT_FALSE(r.config.home_configured);
}

static void test_rejects_files_that_are_not_backups(void) {
  const Config current = sample();
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson("", current).status);
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson("not json", current).status);
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson("[1,2]", current).status);
  // OpenSky's credentials file is JSON too, but not a backup.
  ASSERT_ENUM_EQ(BackupRestoreStatus::WrongFormat, restoreConfigFromBackupJson("{\"clientId\":\"a\",\"clientSecret\":\"b\"}", current) .status);
  // A newer format this firmware doesn't know.
  ASSERT_ENUM_EQ(BackupRestoreStatus::WrongFormat, restoreConfigFromBackupJson("{\"format\":\"cyd-sky-tracker-config\",\"format_version\":2}", current) .status);
  // A rejected file leaves the config untouched.
  BackupRestoreResult r = restoreConfigFromBackupJson("{\"format\":\"other\"}", current);
  TEST_ASSERT_EQUAL_STRING("my-client", r.config.opensky_client_id.c_str());
}

static void test_rejects_oversized_files(void) {
  std::string big = "{\"format\":\"cyd-sky-tracker-config\",\"pad\":\"";
  big += std::string(kMaxBackupJsonBytes, 'x');
  big += "\"}";
  ASSERT_ENUM_EQ(BackupRestoreStatus::NotJson, restoreConfigFromBackupJson(big, sample()).status);
}

static void test_panel_password_travels_only_with_secrets(void) {
  Config c = sample();
  c.web_password = "hunter22";
  assertJsonString(parseJson(configToBackupJson(c, true, "v1")), "security.web_password", "hunter22");
  ASSERT_NOT_CONTAINS(configToBackupJson(c, false, "v1"), "hunter22");
  assertJsonMissing(parseJson(configToBackupJson(c, false, "v1")), "security");
  assertJsonMissing(parseJson(configToBackupJson(sample(), true, "v1")), "security");  // none set

  BackupRestoreResult r = restoreConfigFromBackupJson(configToBackupJson(c, true, "v1"),
                                                      defaultConfig());
  TEST_ASSERT_EQUAL_STRING("hunter22", r.config.web_password.c_str());
  TEST_ASSERT_EQUAL_INT(25, r.appliedFields);
}

static void test_restore_without_password_keeps_the_devices(void) {
  Config device = defaultConfig();
  device.web_password = "device-pass";
  BackupRestoreResult r = restoreConfigFromBackupJson(configToBackupJson(sample(), false, "v1"),
                                                      device);
  TEST_ASSERT_EQUAL_STRING("device-pass", r.config.web_password.c_str());
}

static void test_invalid_password_in_backup_is_reported_and_skipped(void) {
  Config device = defaultConfig();
  device.web_password = "device-pass";
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"security\":{\"web_password\":\"short\"}}",
      device);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_STRING("device-pass", r.config.web_password.c_str());
  TEST_ASSERT_EQUAL_INT(0, r.appliedFields);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("security.web_password", r.invalidFields[0].c_str());
}

static void test_display_group_round_trip(void) {
  Config c = defaultConfig();
  c.brightness_auto = false;
  c.brightness_fixed_pct = 42;
  c.brightness_min_pct = 15;
  c.brightness_max_pct = 90;
  c.night_mode = NightMode::Schedule;
  c.night_from_min = 22 * 60 + 15;
  c.night_to_min = 7 * 60;
  c.night_pct = 0;
  c.night_wake_s = 120;
  c.night_pause_polling = true;
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
  TEST_ASSERT_FALSE(r.config.brightness_auto);
  TEST_ASSERT_EQUAL_UINT8(42, r.config.brightness_fixed_pct);
  TEST_ASSERT_EQUAL_UINT8(15, r.config.brightness_min_pct);
  TEST_ASSERT_EQUAL_UINT8(90, r.config.brightness_max_pct);
  ASSERT_ENUM_EQ(NightMode::Schedule, r.config.night_mode);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 15, r.config.night_from_min);
  TEST_ASSERT_EQUAL_UINT16(7 * 60, r.config.night_to_min);
  TEST_ASSERT_EQUAL_UINT8(0, r.config.night_pct);
  TEST_ASSERT_EQUAL_UINT16(120, r.config.night_wake_s);
  TEST_ASSERT_TRUE(r.config.night_pause_polling);
}

static void test_display_group_bad_values_and_old_backups(void) {
  // Without the group (a backup from before spec/03): current values kept.
  Config current = defaultConfig();
  current.night_pct = 33;
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"general\":{}}", current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT8(33, r.config.night_pct);
  // Wrong values are skipped and listed; a huge number is capped (sanitizeConfig clamps it).
  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"display\":{"
      "\"night_mode\":\"dark\",\"night_from\":\"25:00\",\"night_brightness\":\"x\","
      "\"brightness_max\":70000}}",
      current);
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("display.night_mode", r.invalidFields[0].c_str());
  TEST_ASSERT_EQUAL_UINT8(255, r.config.brightness_max_pct);
  TEST_ASSERT_EQUAL_UINT8(33, r.config.night_pct);
  // An unknown group (a backup from a newer firmware) doesn't reject the file.
  r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"future\":{\"x\":1}}",
      current);
  ASSERT_ENUM_EQ(BackupRestoreStatus::Ok, r.status);
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
}
