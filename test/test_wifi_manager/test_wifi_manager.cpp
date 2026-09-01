#include <unity.h>

#include "wifi_manager.h"

void setUp(void) {}
void tearDown(void) {}

static void test_derive_status_prefers_connected(void) {
  TEST_ASSERT_TRUE(deriveWifiStatus(true, true) == WifiStatus::Connected);
  TEST_ASSERT_TRUE(deriveWifiStatus(true, false) == WifiStatus::Connected);
}

static void test_derive_status_portal_active_when_not_connected(void) {
  TEST_ASSERT_TRUE(deriveWifiStatus(false, true) == WifiStatus::PortalActive);
}

static void test_derive_status_connecting_when_neither(void) {
  TEST_ASSERT_TRUE(deriveWifiStatus(false, false) == WifiStatus::Connecting);
}

static void test_status_label_matches_status(void) {
  TEST_ASSERT_EQUAL_STRING("CONNECTED", wifiStatusLabel(WifiStatus::Connected));
  TEST_ASSERT_EQUAL_STRING("CONNECTING", wifiStatusLabel(WifiStatus::Connecting));
  TEST_ASSERT_EQUAL_STRING("SETUP PORTAL ACTIVE", wifiStatusLabel(WifiStatus::PortalActive));
  TEST_ASSERT_EQUAL_STRING("DISCONNECTED", wifiStatusLabel(WifiStatus::Disconnected));
}

// ---- shouldRestartConnection (portal timeout dead-end, review notes 1.3) --

static void test_should_restart_when_portal_just_closed_unconnected(void) {
  TEST_ASSERT_TRUE(shouldRestartConnection(/*was*/ true, /*now*/ false, /*connected*/ false));
}

static void test_should_not_restart_when_portal_still_active(void) {
  TEST_ASSERT_FALSE(shouldRestartConnection(true, true, false));
}

static void test_should_not_restart_when_portal_closed_because_connected(void) {
  // The portal closed because autoConnect() succeeded, not because it
  // timed out — nothing to restart.
  TEST_ASSERT_FALSE(shouldRestartConnection(true, false, true));
}

static void test_should_not_restart_when_portal_was_never_active(void) {
  TEST_ASSERT_FALSE(shouldRestartConnection(false, false, false));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_derive_status_prefers_connected);
  RUN_TEST(test_derive_status_portal_active_when_not_connected);
  RUN_TEST(test_derive_status_connecting_when_neither);
  RUN_TEST(test_status_label_matches_status);

  RUN_TEST(test_should_restart_when_portal_just_closed_unconnected);
  RUN_TEST(test_should_not_restart_when_portal_still_active);
  RUN_TEST(test_should_not_restart_when_portal_closed_because_connected);
  RUN_TEST(test_should_not_restart_when_portal_was_never_active);
  return UNITY_END();
}
