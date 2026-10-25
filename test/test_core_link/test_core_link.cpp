#include <unity.h>

#include <memory>
#include <string>

#include "core_link.h"

void setUp(void) {}
void tearDown(void) {}

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

static void test_command_without_tag(void) {
  const NetCommand c = makeNetCommand(NetCommandType::RefreshNow);
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

int main(int, char **) {
  UNITY_BEGIN();
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
  RUN_TEST(test_command_without_tag);

  RUN_TEST(test_ms_until_period);
  RUN_TEST(test_ms_until_next_minute);
  RUN_TEST(test_ui_sleep_defaults_to_touch_poll);
  RUN_TEST(test_ui_sleep_takes_nearest_deadline);
  RUN_TEST(test_ui_sleep_is_capped);

  RUN_TEST(test_heartbeat_stale);
  return UNITY_END();
}
