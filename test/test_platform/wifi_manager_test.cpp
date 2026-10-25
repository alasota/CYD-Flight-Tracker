// wifi_manager: WiFi status derivation. CLAUDE.md "Code conventions" (wifi_manager).
#include <unity.h>

#include "test_support.h"
#include "wifi_manager.h"

namespace {

static void test_derive_status_prefers_connected(void) {
  ASSERT_ENUM_EQ(WifiStatus::Connected, deriveWifiStatus(true, true));
  ASSERT_ENUM_EQ(WifiStatus::Connected, deriveWifiStatus(true, false));
}

static void test_derive_status_portal_active_when_not_connected(void) {
  ASSERT_ENUM_EQ(WifiStatus::PortalActive, deriveWifiStatus(false, true));
}

static void test_derive_status_connecting_when_neither(void) {
  ASSERT_ENUM_EQ(WifiStatus::Connecting, deriveWifiStatus(false, false));
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

// ---- shouldAttemptReconnect (link-loss backstop, review notes 1.5) -------

static void test_no_reconnect_while_connected(void) {
  TEST_ASSERT_FALSE(shouldAttemptReconnect(/*connected*/ true, /*portal*/ false, 999999, 999999));
}

static void test_no_reconnect_while_portal_open(void) {
  TEST_ASSERT_FALSE(shouldAttemptReconnect(false, /*portal*/ true, 999999, 999999));
}

static void test_no_reconnect_inside_grace_period(void) {
  // Down, but only briefly — leave it to the core's own auto-reconnect.
  TEST_ASSERT_FALSE(shouldAttemptReconnect(false, false, kReconnectGraceMs - 1, 999999));
}

static void test_no_reconnect_too_soon_after_last_attempt(void) {
  TEST_ASSERT_FALSE(
      shouldAttemptReconnect(false, false, 999999, kReconnectIntervalMs - 1));
}

static void test_reconnect_when_down_long_enough_and_interval_elapsed(void) {
  TEST_ASSERT_TRUE(
      shouldAttemptReconnect(false, false, kReconnectGraceMs, kReconnectIntervalMs));
  TEST_ASSERT_TRUE(shouldAttemptReconnect(false, false, 120000, 60000));
}

}  // namespace

void run_wifi_manager_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_derive_status_prefers_connected);
  RUN_TEST(test_derive_status_portal_active_when_not_connected);
  RUN_TEST(test_derive_status_connecting_when_neither);
  RUN_TEST(test_status_label_matches_status);

  RUN_TEST(test_should_restart_when_portal_just_closed_unconnected);
  RUN_TEST(test_should_not_restart_when_portal_still_active);
  RUN_TEST(test_should_not_restart_when_portal_closed_because_connected);
  RUN_TEST(test_should_not_restart_when_portal_was_never_active);

  RUN_TEST(test_no_reconnect_while_connected);
  RUN_TEST(test_no_reconnect_while_portal_open);
  RUN_TEST(test_no_reconnect_inside_grace_period);
  RUN_TEST(test_no_reconnect_too_soon_after_last_attempt);
  RUN_TEST(test_reconnect_when_down_long_enough_and_interval_elapsed);
}
