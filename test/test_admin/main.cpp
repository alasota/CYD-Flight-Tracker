// Suite test_admin: settings, web panel, backups, password, updates. See test/README.md.
#include <unity.h>

#include "test_support.h"

void run_config_page_tests();
void run_config_portal_tests();
void run_config_backup_tests();
void run_config_store_tests();
void run_web_auth_tests();
void run_web_i18n_tests();
void run_ota_update_tests();
void run_release_check_tests();
void run_version_tests();
void run_screenshot_tests();

void setUp(void) { resetAllForTesting(); }
void tearDown(void) {}

// -DTEST_MODULE=<module> runs one module's tests (test/README.md). Not
// TEST_ONLY as spec 07 first said: unity.h already defines TEST_ONLY().
#ifdef TEST_MODULE
#define RUN_MODULE(m) testModuleIs(TEST_SUPPORT_STR(TEST_MODULE), #m)
#else
#define RUN_MODULE(m) true
#endif

int main(int, char **) {
  UNITY_BEGIN();
  if (RUN_MODULE(config_page)) run_config_page_tests();
  if (RUN_MODULE(config_portal)) run_config_portal_tests();
  if (RUN_MODULE(config_backup)) run_config_backup_tests();
  if (RUN_MODULE(config_store)) run_config_store_tests();
  if (RUN_MODULE(web_auth)) run_web_auth_tests();
  if (RUN_MODULE(web_i18n)) run_web_i18n_tests();
  if (RUN_MODULE(ota_update)) run_ota_update_tests();
  if (RUN_MODULE(release_check)) run_release_check_tests();
  if (RUN_MODULE(version)) run_version_tests();
  if (RUN_MODULE(screenshot)) run_screenshot_tests();
  return UNITY_END();
}
