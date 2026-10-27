// core_link: LatestBox, Guarded, NetState diff, UI sleep, pending commands. CLAUDE.md "Concurrency — two cores", spec/04, spec 09.
#include <unity.h>

#include <memory>
#include <string>
#include <vector>

#include "core_link.h"
#include "test_support.h"

namespace {

// ---- LatestBox -------------------------------------------------------------

// Records whether it is held — so a value's destructor can check it doesn't
// run under the lock.
struct SpyLock {
  bool held = false;
  int locks = 0;
  void lock() {
    held = true;
    ++locks;
  }
  void unlock() { held = false; }
};

static SpyLock *activeSpy = nullptr;
static int destroyedUnderLock = 0;
static int destroyed = 0;

struct Tracked {
  int v = 0;
  explicit Tracked(int x) : v(x) {}
  ~Tracked() {
    ++destroyed;
    if (activeSpy != nullptr && activeSpy->held) ++destroyedUnderLock;
  }
};

static void test_box_starts_empty(void) {
  LatestBox<int, SpyLock> box;
  uint32_t seen = 0;
  std::shared_ptr<const int> out;
  TEST_ASSERT_FALSE(box.takeIfNewer(seen, out));
  TEST_ASSERT_NULL(out.get());
}

static void test_box_publish_take(void) {
  LatestBox<int, SpyLock> box;
  uint32_t seen = 0;
  std::shared_ptr<const int> out;
  box.publish(std::make_shared<const int>(7));
  TEST_ASSERT_TRUE(box.takeIfNewer(seen, out));
  TEST_ASSERT_EQUAL_INT(7, *out);
  TEST_ASSERT_EQUAL_UINT32(1, seen);
  // Nothing new: false, and `out` keeps what it had.
  TEST_ASSERT_FALSE(box.takeIfNewer(seen, out));
  TEST_ASSERT_EQUAL_INT(7, *out);
}

static void test_box_only_latest_counts(void) {
  LatestBox<int, SpyLock> box;
  uint32_t seen = 0;
  std::shared_ptr<const int> out;
  box.publish(std::make_shared<const int>(1));
  box.publish(std::make_shared<const int>(2));
  box.publish(std::make_shared<const int>(3));
  TEST_ASSERT_TRUE(box.takeIfNewer(seen, out));
  TEST_ASSERT_EQUAL_INT(3, *out);  // the in-between ones were never seen — fine
  TEST_ASSERT_FALSE(box.takeIfNewer(seen, out));
}

static void test_box_reader_keeps_value_alive(void) {
  LatestBox<Tracked, SpyLock> box;
  destroyed = 0;
  uint32_t seen = 0;
  std::shared_ptr<const Tracked> out;
  box.publish(std::make_shared<const Tracked>(1));
  TEST_ASSERT_TRUE(box.takeIfNewer(seen, out));
  box.publish(std::make_shared<const Tracked>(2));
  TEST_ASSERT_EQUAL_INT(0, destroyed);  // the reader still holds #1
  TEST_ASSERT_EQUAL_INT(1, out->v);
  TEST_ASSERT_TRUE(box.takeIfNewer(seen, out));  // drops #1
  TEST_ASSERT_EQUAL_INT(1, destroyed);
  TEST_ASSERT_EQUAL_INT(2, out->v);
}

static void test_box_never_destroys_under_lock(void) {
  LatestBox<Tracked, SpyLock> box;
  activeSpy = &box.lockForTesting();
  destroyedUnderLock = 0;
  destroyed = 0;
  uint32_t seen = 0;
  std::shared_ptr<const Tracked> out;
  box.publish(std::make_shared<const Tracked>(1));
  box.publish(std::make_shared<const Tracked>(2));  // #1 dies (nobody held it)
  TEST_ASSERT_TRUE(box.takeIfNewer(seen, out));
  box.publish(std::make_shared<const Tracked>(3));
  TEST_ASSERT_TRUE(box.takeIfNewer(seen, out));  // #2 dies in the reader
  TEST_ASSERT_EQUAL_INT(2, destroyed);
  TEST_ASSERT_EQUAL_INT(0, destroyedUnderLock);
  TEST_ASSERT_FALSE(box.lockForTesting().held);
  activeSpy = nullptr;
}

static void test_box_miss_is_one_lock(void) {
  LatestBox<int, SpyLock> box;
  box.publish(std::make_shared<const int>(1));
  uint32_t seen = box.seq();
  std::shared_ptr<const int> out;
  const int before = box.lockForTesting().locks;
  TEST_ASSERT_FALSE(box.takeIfNewer(seen, out));
  TEST_ASSERT_EQUAL_INT(before + 1, box.lockForTesting().locks);
}

static void test_guarded_copy(void) {
  struct Pod {
    int a;
    float b;
  };
  Guarded<Pod, SpyLock> g;
  TEST_ASSERT_EQUAL_INT(0, g.get().a);
  g.set(Pod{3, 1.5f});
  TEST_ASSERT_EQUAL_INT(3, g.get().a);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, g.get().b);
}

// ---- diffNetState ------------------------------------------------------------

static NetState baseState() {
  NetState s;
  auto cfg = std::make_shared<Config>();
  cfg->home_configured = true;
  s.cfg = cfg;
  auto d = std::make_shared<DataSnapshot>();
  d->rows.resize(2);
  s.data = d;
  return s;
}

static bool any(const RedrawEvents &e) {
  return e.firstDraw || e.screenChanged || e.pageChanged || e.languageChanged || e.newPoll ||
         e.minuteChanged || e.healthChanged || e.countdownTick;
}

static void test_diff_first_snapshot_is_first_draw(void) {
  const NetState s = baseState();
  TEST_ASSERT_TRUE(diffNetState(nullptr, s).firstDraw);
}

static void test_diff_identical_is_nothing(void) {
  const NetState a = baseState();
  NetState b = a;  // same pointers
  b.update.status = UpdateStatus::UpToDate;  // not a display matter
  b.wifi = WifiStatus::Connected;
  TEST_ASSERT_FALSE(any(diffNetState(&a, b)));
}

static void test_diff_new_data_is_new_poll(void) {
  const NetState a = baseState();
  NetState b = a;
  b.data = std::make_shared<DataSnapshot>(*a.data);  // same content, new poll
  const RedrawEvents e = diffNetState(&a, b);
  TEST_ASSERT_TRUE(e.newPoll);
  TEST_ASSERT_FALSE(e.healthChanged);
}

static void test_diff_health_with_rows_is_header_only(void) {
  const NetState a = baseState();
  NetState b = a;
  b.health = OpenSkyHealth::RateLimited;
  const RedrawEvents e = diffNetState(&a, b);
  TEST_ASSERT_TRUE(e.healthChanged);
  TEST_ASSERT_FALSE(e.newPoll);
}

static void test_diff_health_on_empty_screen_repaints_content(void) {
  NetState a = baseState();
  a.data = nullptr;  // no data yet: the empty-state text depends on health
  NetState b = a;
  b.health = OpenSkyHealth::AuthError;
  const RedrawEvents e = diffNetState(&a, b);
  TEST_ASSERT_TRUE(e.healthChanged);
  TEST_ASSERT_TRUE(e.newPoll);

  NetState c = baseState();
  c.data = std::make_shared<DataSnapshot>();  // polled, nothing in range
  NetState d = c;
  d.health = OpenSkyHealth::NetworkError;
  TEST_ASSERT_TRUE(diffNetState(&c, d).newPoll);
}

static void test_diff_language_change(void) {
  const NetState a = baseState();
  NetState b = a;
  auto cfg = std::make_shared<Config>(*a.cfg);
  cfg->language = a.cfg->language == Language::PL ? Language::EN : Language::PL;
  b.cfg = cfg;
  TEST_ASSERT_TRUE(diffNetState(&a, b).languageChanged);
}

static void test_diff_home_configured_is_first_draw(void) {
  NetState a = baseState();
  auto unset = std::make_shared<Config>(*a.cfg);
  unset->home_configured = false;
  a.cfg = unset;
  NetState b = a;
  auto set = std::make_shared<Config>(*a.cfg);
  set->home_configured = true;
  b.cfg = set;
  TEST_ASSERT_TRUE(diffNetState(&a, b).firstDraw);
}

static void test_diff_radius_or_home_moves_content(void) {
  const NetState a = baseState();
  NetState b = a;
  auto cfg = std::make_shared<Config>(*a.cfg);
  cfg->radius_deg = a.cfg->radius_deg + 1.0f;
  b.cfg = cfg;
  TEST_ASSERT_TRUE(diffNetState(&a, b).newPoll);

  NetState c = a;
  auto moved = std::make_shared<Config>(*a.cfg);
  moved->home_lat += 0.5f;
  c.cfg = moved;
  TEST_ASSERT_TRUE(diffNetState(&a, c).newPoll);
}

static void test_diff_unrelated_config_change_is_nothing(void) {
  const NetState a = baseState();
  NetState b = a;
  auto cfg = std::make_shared<Config>(*a.cfg);
  cfg->poll_interval_s = a.cfg->poll_interval_s + 10;
  b.cfg = cfg;
  TEST_ASSERT_FALSE(any(diffNetState(&a, b)));
}

static void test_differs(void) {
  const NetState a = baseState();
  NetState b = a;
  TEST_ASSERT_FALSE(netStateDiffers(a, b));
  b.health = OpenSkyHealth::AuthError;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.data = std::make_shared<DataSnapshot>();
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.update.latestTag = "v9.9.9";
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.update.installPending = true;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.ota.seq++;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.wifi = WifiStatus::PortalActive;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.restartPending = true;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
}

// ---- NetCommand ------------------------------------------------------------

static void test_command_tag_round_trip(void) {
  const NetCommand c = makeNetCommand(NetCommandType::InstallTag, "v1.3.0");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(NetCommandType::InstallTag), static_cast<int>(c.type));
  TEST_ASSERT_EQUAL_STRING("v1.3.0", netCommandTag(c).c_str());
}

static void test_command_long_tag_is_cut_and_terminated(void) {
  const std::string longTag(100, 'x');
  const NetCommand c = makeNetCommand(NetCommandType::SkipTag, longTag);
  TEST_ASSERT_EQUAL_size_t(sizeof(c.tag) - 1, netCommandTag(c).size());
  TEST_ASSERT_EQUAL_CHAR('\0', c.tag[sizeof(c.tag) - 1]);
}

// Scenario: Domyślnie utworzona komenda sieciowa nic nie zleca
static void test_default_net_command_is_none(void) {
  const NetCommand c;
  ASSERT_ENUM_EQ(NetCommandType::None, c.type);
  ASSERT_STR_EQ("", netCommandTag(c));
}

static void test_command_without_tag(void) {
  const NetCommand c = makeNetCommand(NetCommandType::SkipTag);
  TEST_ASSERT_EQUAL_STRING("", netCommandTag(c).c_str());
}

// ---- UI pacing ---------------------------------------------------------------

static void test_ms_until_period(void) {
  TEST_ASSERT_EQUAL_UINT32(1000, msUntilPeriod(5000, 5000, 1000));
  TEST_ASSERT_EQUAL_UINT32(400, msUntilPeriod(5600, 5000, 1000));
  TEST_ASSERT_EQUAL_UINT32(0, msUntilPeriod(6000, 5000, 1000));
  TEST_ASSERT_EQUAL_UINT32(0, msUntilPeriod(9000, 5000, 1000));
  // Across a millis() rollover: last just before the wrap, now just after.
  TEST_ASSERT_EQUAL_UINT32(644, msUntilPeriod(100u, 0xFFFFFF00u, 1000));  // 356 ms elapsed
}

static void test_ms_until_next_minute(void) {
  TEST_ASSERT_EQUAL_UINT32(60000, msUntilNextMinute(0, 0));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextMinute(59, 999));
  TEST_ASSERT_EQUAL_UINT32(30500, msUntilNextMinute(29, 500));
  TEST_ASSERT_EQUAL_UINT32(1, msUntilNextMinute(61, 5000));  // clamped garbage
}

static void test_ui_sleep_defaults_to_touch_poll(void) {
  UiWake w;
  TEST_ASSERT_EQUAL_UINT32(kUiTouchPollMs, uiSleepMs(w));
}

static void test_ui_sleep_takes_nearest_deadline(void) {
  UiWake w;
  w.untilCountdownMs = 7;
  w.untilMinuteMs = 3000;
  TEST_ASSERT_EQUAL_UINT32(7, uiSleepMs(w));
  w.untilMinuteMs = 2;
  TEST_ASSERT_EQUAL_UINT32(2, uiSleepMs(w));
  w.untilPromptTimerMs = 0;  // overdue -> go now
  TEST_ASSERT_EQUAL_UINT32(0, uiSleepMs(w));
  UiWake b;
  b.untilBacklightMs = 5;
  TEST_ASSERT_EQUAL_UINT32(5, uiSleepMs(b));
}

static void test_ui_sleep_is_capped(void) {
  UiWake w;
  w.touchPollMs = kNoDeadline;  // no reason to wake at all
  TEST_ASSERT_EQUAL_UINT32(kUiMaxSleepMs, uiSleepMs(w));
}

// ---- liveness ----------------------------------------------------------------

static void test_heartbeat_stale(void) {
  TEST_ASSERT_FALSE(heartbeatStale(10000, 9000, kUiAliveLimitMs));
  TEST_ASSERT_FALSE(heartbeatStale(11000, 9000, kUiAliveLimitMs));  // exactly the limit
  TEST_ASSERT_TRUE(heartbeatStale(11001, 9000, kUiAliveLimitMs));
  TEST_ASSERT_TRUE(heartbeatStale(70000, 1000, kNetStallLimitMs));
  TEST_ASSERT_FALSE(heartbeatStale(500u, 0xFFFFFF00u, kUiAliveLimitMs));  // rollover
}


// ---- device alerts in NetState (spec 08 §3.3) --------------------------------

static void test_differs_on_alerts_and_their_details(void) {
  const NetState a = baseState();
  NetState b = a;
  b.alerts.set(AlertId::OpenSkyBudgetOverLimit);
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.alertDetails.estimatedDailyCredits = 5760;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.alertDetails.loginHttpCode = 401;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
  b = a;
  b.alertDetails.quotaResetInS = 600;
  TEST_ASSERT_TRUE(netStateDiffers(a, b));
}

static void test_quota_observed_ms_alone_is_no_publish(void) {
  const NetState a = baseState();
  NetState b = a;
  b.alertDetails.quotaObservedMs = 123456;
  TEST_ASSERT_FALSE(netStateDiffers(a, b));
}

static void test_diff_alerts_changed_only_when_the_badge_changes(void) {
  NetState a = baseState();
  NetState b = a;
  b.alerts.set(AlertId::OpenSkyBudgetOverLimit);  // None -> Warning
  RedrawEvents e = diffNetState(&a, b);
  TEST_ASSERT_TRUE(e.alertsChanged);
  TEST_ASSERT_FALSE(e.newPoll);
  a = b;
  b.alerts.set(AlertId::OpenSkyQuotaExhausted);  // Warning -> Error
  TEST_ASSERT_TRUE(diffNetState(&a, b).alertsChanged);
  a = b;
  b.alerts.set(AlertId::OpenSkyLoginFailed);  // still Error: same badge
  TEST_ASSERT_FALSE(diffNetState(&a, b).alertsChanged);
  a = b;
  b.alertDetails.estimatedDailyCredits = 1;  // details aren't drawn in the header
  TEST_ASSERT_FALSE(diffNetState(&a, b).alertsChanged);
}

static void test_ui_sleep_wakes_for_the_alert_countdown(void) {
  UiWake w;
  w.untilAlertTimerMs = 3;
  TEST_ASSERT_EQUAL_UINT32(3, uiSleepMs(w));
}

// ---- A command the queue didn't take waits and is retried (spec 09 §2) ---------

// A fake FreeRTOS queue: refuses while `full`, records what it accepted and
// how often it was asked.
struct FakeQueue {
  bool full = false;
  int attempts = 0;
  std::vector<NetCommand> accepted;
  CommandSender sender() {
    return [this](const NetCommand &c) {
      ++attempts;
      if (full) return false;
      accepted.push_back(c);
      return true;
    };
  }
};

static void assertCommand(NetCommandType type, const char *tag, const NetCommand &c) {
  ASSERT_ENUM_EQ(type, c.type);
  ASSERT_STR_EQ(tag, netCommandTag(c));
}

// Odpowiedź na pytanie o aktualizację czeka, aż kolejka ją przyjmie
// (Examples: NIE -> SkipTag, TAK -> InstallTag)
static void test_pending_command_sent_once_queue_accepts(void) {
  for (NetCommandType type : {NetCommandType::SkipTag, NetCommandType::InstallTag}) {
    FakeQueue q;
    q.full = true;
    PendingCommand p = submitCommand(makeNetCommand(type, "v1.8.0"), q.sender());
    TEST_ASSERT_TRUE(p.waiting);
    assertCommand(type, "v1.8.0", p.cmd);

    for (int pass = 0; pass < 3; ++pass) {  // loop() passes while the queue is full
      const int before = q.attempts;
      p = retryPendingCommand(p, q.sender());
      TEST_ASSERT_EQUAL_INT(before + 1, q.attempts);  // retried on every pass
      TEST_ASSERT_TRUE(p.waiting);
    }

    q.full = false;
    p = retryPendingCommand(p, q.sender());
    TEST_ASSERT_FALSE(p.waiting);
    p = retryPendingCommand(p, q.sender());  // and nothing more after that
    TEST_ASSERT_EQUAL_size_t(1, q.accepted.size());
    assertCommand(type, "v1.8.0", q.accepted[0]);
  }
}

// Dopóki kolejka jest pełna, oczekująca komenda nie ginie
static void test_pending_command_kept_while_queue_full(void) {
  FakeQueue q;
  q.full = true;
  PendingCommand p;
  p.waiting = true;
  p.cmd = makeNetCommand(NetCommandType::SkipTag, "v1.8.0");
  for (int i = 0; i < 100; ++i) {
    p = retryPendingCommand(p, q.sender());
    TEST_ASSERT_TRUE(p.waiting);
    assertCommand(NetCommandType::SkipTag, "v1.8.0", p.cmd);
  }
  TEST_ASSERT_EQUAL_INT(100, q.attempts);
  TEST_ASSERT_EQUAL_size_t(0, q.accepted.size());
}

// Bez oczekującej komendy ponawianie nic nie wysyła
static void test_no_pending_command_sends_nothing(void) {
  FakeQueue q;
  const PendingCommand p = retryPendingCommand(PendingCommand{}, q.sender());
  TEST_ASSERT_EQUAL_INT(0, q.attempts);
  TEST_ASSERT_FALSE(p.waiting);
  TEST_ASSERT_FALSE(p.logQueueFull);
}

// „command queue full, retrying” trafia do logu raz na komendę
static void test_queue_full_logged_once_per_command(void) {
  FakeQueue q;
  q.full = true;
  PendingCommand p = submitCommand(makeNetCommand(NetCommandType::SkipTag, "v1.8.0"), q.sender());
  TEST_ASSERT_TRUE(p.logQueueFull);  // the first failed attempt
  for (int i = 0; i < 10; ++i) {
    p = retryPendingCommand(p, q.sender());
    TEST_ASSERT_TRUE(p.waiting);
    TEST_ASSERT_FALSE(p.logQueueFull);
  }
}

// Kolejka ma miejsce — komenda idzie od razu, nic nie czeka
static void test_command_sent_directly_when_queue_has_room(void) {
  FakeQueue q;
  const PendingCommand p =
      submitCommand(makeNetCommand(NetCommandType::SkipTag, "v1.8.0"), q.sender());
  TEST_ASSERT_EQUAL_size_t(1, q.accepted.size());
  assertCommand(NetCommandType::SkipTag, "v1.8.0", q.accepted[0]);
  TEST_ASSERT_FALSE(p.waiting);
  TEST_ASSERT_FALSE(p.logQueueFull);
}

// Edge (plan): a tag longer than the slot is cut by makeNetCommand — the
// waiting command keeps exactly that POD, byte for byte.
static void test_pending_command_keeps_a_cut_long_tag(void) {
  FakeQueue q;
  q.full = true;
  const NetCommand original = makeNetCommand(NetCommandType::InstallTag, std::string(40, 'v'));
  PendingCommand p = submitCommand(original, q.sender());
  TEST_ASSERT_TRUE(p.waiting);
  q.full = false;
  p = retryPendingCommand(p, q.sender());
  TEST_ASSERT_EQUAL_size_t(1, q.accepted.size());
  TEST_ASSERT_EQUAL_MEMORY(&original, &q.accepted[0], sizeof(NetCommand));
  TEST_ASSERT_EQUAL_size_t(sizeof(original.tag) - 1, netCommandTag(q.accepted[0]).size());
}

// Edge (owner's answer, open question 2): a newer command replaces a waiting
// one; only the newer is sent, and its own first failure is logged.
static void test_newer_command_replaces_waiting_one(void) {
  FakeQueue q;
  q.full = true;
  PendingCommand p = submitCommand(makeNetCommand(NetCommandType::SkipTag, "v1.8.0"), q.sender());
  TEST_ASSERT_TRUE(p.waiting);
  p = submitCommand(makeNetCommand(NetCommandType::InstallTag, "v1.9.0"), q.sender());
  TEST_ASSERT_TRUE(p.waiting);
  TEST_ASSERT_TRUE(p.logQueueFull);
  assertCommand(NetCommandType::InstallTag, "v1.9.0", p.cmd);
  q.full = false;
  p = retryPendingCommand(p, q.sender());
  TEST_ASSERT_FALSE(p.waiting);
  TEST_ASSERT_EQUAL_size_t(1, q.accepted.size());
  assertCommand(NetCommandType::InstallTag, "v1.9.0", q.accepted[0]);
}

}  // namespace

void run_core_link_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_box_starts_empty);
  RUN_TEST(test_box_publish_take);
  RUN_TEST(test_box_only_latest_counts);
  RUN_TEST(test_box_reader_keeps_value_alive);
  RUN_TEST(test_box_never_destroys_under_lock);
  RUN_TEST(test_box_miss_is_one_lock);
  RUN_TEST(test_guarded_copy);

  RUN_TEST(test_diff_first_snapshot_is_first_draw);
  RUN_TEST(test_diff_identical_is_nothing);
  RUN_TEST(test_diff_new_data_is_new_poll);
  RUN_TEST(test_diff_health_with_rows_is_header_only);
  RUN_TEST(test_diff_health_on_empty_screen_repaints_content);
  RUN_TEST(test_diff_language_change);
  RUN_TEST(test_diff_home_configured_is_first_draw);
  RUN_TEST(test_diff_radius_or_home_moves_content);
  RUN_TEST(test_diff_unrelated_config_change_is_nothing);

  RUN_TEST(test_differs);
  RUN_TEST(test_command_tag_round_trip);
  RUN_TEST(test_command_long_tag_is_cut_and_terminated);
  RUN_TEST(test_default_net_command_is_none);
  RUN_TEST(test_command_without_tag);
  RUN_TEST(test_pending_command_sent_once_queue_accepts);
  RUN_TEST(test_pending_command_kept_while_queue_full);
  RUN_TEST(test_no_pending_command_sends_nothing);
  RUN_TEST(test_queue_full_logged_once_per_command);
  RUN_TEST(test_command_sent_directly_when_queue_has_room);
  RUN_TEST(test_pending_command_keeps_a_cut_long_tag);
  RUN_TEST(test_newer_command_replaces_waiting_one);

  RUN_TEST(test_ms_until_period);
  RUN_TEST(test_ms_until_next_minute);
  RUN_TEST(test_ui_sleep_defaults_to_touch_poll);
  RUN_TEST(test_ui_sleep_takes_nearest_deadline);
  RUN_TEST(test_ui_sleep_is_capped);

  RUN_TEST(test_heartbeat_stale);
  RUN_TEST(test_differs_on_alerts_and_their_details);
  RUN_TEST(test_quota_observed_ms_alone_is_no_publish);
  RUN_TEST(test_diff_alerts_changed_only_when_the_badge_changes);
  RUN_TEST(test_ui_sleep_wakes_for_the_alert_countdown);
}
