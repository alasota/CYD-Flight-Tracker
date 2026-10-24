#include <unity.h>

#include <string>

#include "config_backup.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

Config sample() {
  Config c = defaultConfig();
  c.home_lat = 52.2297f;
  c.home_lon = 21.0122f;
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
  c.home_configured = true;
  c.update_check_enabled = false;
  c.update_show_prerelease = true;
  c.update_skipped_tag = "v1.4.0";  // device state: never in a backup
  return c;
}

bool contains(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

}  // namespace

static void test_round_trip_restores_every_setting(void) {
  const Config original = sample();
  const std::string json = configToBackupJson(original, true, "v1.2.3");
  BackupRestoreResult r = restoreConfigFromBackupJson(json, defaultConfig());

  TEST_ASSERT_TRUE(r.status == BackupRestoreStatus::Ok);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_INT(14, r.appliedFields);
  const Config &c = r.config;
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, original.home_lat, c.home_lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, original.home_lon, c.home_lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.5f, c.radius_deg);
  TEST_ASSERT_EQUAL_UINT32(30u, c.poll_interval_s);
  TEST_ASSERT_EQUAL_STRING("my-client", c.opensky_client_id.c_str());
  TEST_ASSERT_EQUAL_STRING("s3cr\"et", c.opensky_client_secret.c_str());
  TEST_ASSERT_TRUE(c.language == Language::EN);
  TEST_ASSERT_TRUE(c.web_language == Language::PL);
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
  TEST_ASSERT_TRUE(contains(json, "\"update_check\": false"));
  TEST_ASSERT_TRUE(contains(json, "\"update_show_prerelease\": true"));
  TEST_ASSERT_FALSE(contains(json, "v1.4.0"));
  // An older backup without the keys keeps the current values.
  Config current = defaultConfig();
  current.update_check_enabled = false;
  BackupRestoreResult r = restoreConfigFromBackupJson(
      "{\"format\":\"cyd-sky-tracker-config\",\"format_version\":1,\"general\":{}}", current);
  TEST_ASSERT_TRUE(r.status == BackupRestoreStatus::Ok);
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
  const std::string json = configToBackupJson(sample(), true, "v1.2.3");
  TEST_ASSERT_TRUE(contains(json, "\"format\": \"cyd-sky-tracker-config\""));
  TEST_ASSERT_TRUE(contains(json, "\"format_version\": 1"));
  TEST_ASSERT_TRUE(contains(json, "\"firmware\": \"v1.2.3\""));
  TEST_ASSERT_TRUE(contains(json, "\"language\": \"en\""));
}

static void test_backup_without_secrets_keeps_the_devices_credentials(void) {
  const std::string json = configToBackupJson(sample(), false, "v1");
  TEST_ASSERT_FALSE(contains(json, "client_id"));
  TEST_ASSERT_FALSE(contains(json, "client_secret"));
  TEST_ASSERT_FALSE(contains(json, "s3cr"));

  Config device = defaultConfig();
  device.opensky_client_id = "device-id";
  device.opensky_client_secret = "device-secret";
  BackupRestoreResult r = restoreConfigFromBackupJson(json, device);
  TEST_ASSERT_TRUE(r.status == BackupRestoreStatus::Ok);
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
  TEST_ASSERT_TRUE(r.status == BackupRestoreStatus::Ok);
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
  TEST_ASSERT_TRUE(r.status == BackupRestoreStatus::Ok);
  TEST_ASSERT_EQUAL_INT(1, r.appliedFields);  // only location.lon
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.5f, r.config.home_lon);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, current.home_lat, r.config.home_lat);  // kept
  TEST_ASSERT_EQUAL_UINT32(30u, r.config.poll_interval_s);                // kept
  TEST_ASSERT_TRUE(r.config.language == Language::EN);                    // kept
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
  TEST_ASSERT_TRUE(restoreConfigFromBackupJson("", current).status == BackupRestoreStatus::NotJson);
  TEST_ASSERT_TRUE(restoreConfigFromBackupJson("not json", current).status ==
                   BackupRestoreStatus::NotJson);
  TEST_ASSERT_TRUE(restoreConfigFromBackupJson("[1,2]", current).status ==
                   BackupRestoreStatus::NotJson);
  // OpenSky's credentials file is JSON too, but not a backup.
  TEST_ASSERT_TRUE(restoreConfigFromBackupJson("{\"clientId\":\"a\",\"clientSecret\":\"b\"}", current)
                       .status == BackupRestoreStatus::WrongFormat);
  // A newer format this firmware doesn't know.
  TEST_ASSERT_TRUE(
      restoreConfigFromBackupJson("{\"format\":\"cyd-sky-tracker-config\",\"format_version\":2}",
                                  current)
          .status == BackupRestoreStatus::WrongFormat);
  // A rejected file leaves the config untouched.
  BackupRestoreResult r = restoreConfigFromBackupJson("{\"format\":\"other\"}", current);
  TEST_ASSERT_EQUAL_STRING("my-client", r.config.opensky_client_id.c_str());
}

static void test_rejects_oversized_files(void) {
  std::string big = "{\"format\":\"cyd-sky-tracker-config\",\"pad\":\"";
  big += std::string(kMaxBackupJsonBytes, 'x');
  big += "\"}";
  TEST_ASSERT_TRUE(restoreConfigFromBackupJson(big, sample()).status == BackupRestoreStatus::NotJson);
}

static void test_panel_password_travels_only_with_secrets(void) {
  Config c = sample();
  c.web_password = "hunter22";
  TEST_ASSERT_TRUE(contains(configToBackupJson(c, true, "v1"), "\"web_password\": \"hunter22\""));
  TEST_ASSERT_FALSE(contains(configToBackupJson(c, false, "v1"), "hunter22"));
  TEST_ASSERT_FALSE(contains(configToBackupJson(sample(), true, "v1"), "security"));  // none set

  BackupRestoreResult r = restoreConfigFromBackupJson(configToBackupJson(c, true, "v1"),
                                                      defaultConfig());
  TEST_ASSERT_EQUAL_STRING("hunter22", r.config.web_password.c_str());
  TEST_ASSERT_EQUAL_INT(15, r.appliedFields);
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
  TEST_ASSERT_TRUE(r.status == BackupRestoreStatus::Ok);
  TEST_ASSERT_EQUAL_STRING("device-pass", r.config.web_password.c_str());
  TEST_ASSERT_EQUAL_INT(0, r.appliedFields);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.invalidFields.size()));
  TEST_ASSERT_EQUAL_STRING("security.web_password", r.invalidFields[0].c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_restores_every_setting);
  RUN_TEST(test_update_settings_in_the_general_group);
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
  return UNITY_END();
}
