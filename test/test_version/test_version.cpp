#include <unity.h>

#include <cstring>

#include "version.h"

void setUp(void) {}
void tearDown(void) {}

static void test_version_falls_back_when_not_injected(void) {
  // The native env doesn't run tools/pio_version.py.
  TEST_ASSERT_EQUAL_STRING(kFirmwareVersionUnknown, firmwareVersion());
}

static void test_version_is_safe_for_html_and_file_names(void) {
  const char *v = firmwareVersion();
  TEST_ASSERT_TRUE(std::strlen(v) > 0);
  TEST_ASSERT_NULL(std::strpbrk(v, "\"<>&/\\ "));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_version_falls_back_when_not_injected);
  RUN_TEST(test_version_is_safe_for_html_and_file_names);
  return UNITY_END();
}
