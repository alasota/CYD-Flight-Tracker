#include <unity.h>

#include <string>

#include "perf_stats.h"

void setUp(void) {}
void tearDown(void) {}

static void test_empty_gap_stats(void) {
  GapStats g;
  TEST_ASSERT_EQUAL_UINT32(0, g.count);
  TEST_ASSERT_EQUAL_UINT32(0, g.maxMs);
  TEST_ASSERT_EQUAL_UINT32(0, g.percentileMs(99));
  TEST_ASSERT_EQUAL_UINT32(0, g.slowCount());
}

static void test_gaps_land_in_their_buckets(void) {
  GapStats g;
  g.add(0);
  g.add(19);    // <20
  g.add(20);    // <50 (bounds are exclusive upper limits)
  g.add(99);    // <100
  g.add(249);   // <250
  g.add(250);   // <1000
  g.add(5000);  // >=1000
  TEST_ASSERT_EQUAL_UINT32(2, g.buckets[0]);
  TEST_ASSERT_EQUAL_UINT32(1, g.buckets[1]);
  TEST_ASSERT_EQUAL_UINT32(1, g.buckets[2]);
  TEST_ASSERT_EQUAL_UINT32(1, g.buckets[3]);
  TEST_ASSERT_EQUAL_UINT32(1, g.buckets[4]);
  TEST_ASSERT_EQUAL_UINT32(1, g.buckets[5]);
  TEST_ASSERT_EQUAL_UINT32(7, g.count);
  TEST_ASSERT_EQUAL_UINT32(5000, g.maxMs);
  TEST_ASSERT_EQUAL_UINT32(2, g.slowCount());  // 250 and 5000
}

static void test_percentile_from_buckets(void) {
  GapStats g;
  for (int i = 0; i < 99; ++i) g.add(5);
  g.add(3000);
  TEST_ASSERT_EQUAL_UINT32(20, g.percentileMs(99));   // 99 of 100 below 20 ms
  TEST_ASSERT_EQUAL_UINT32(20, g.percentileMs(50));
  TEST_ASSERT_EQUAL_UINT32(3000, g.percentileMs(100));  // open-ended bucket -> max
}

static void test_percentile_rounds_up(void) {
  GapStats g;
  g.add(5);
  g.add(60);  // 2 samples: p99 needs both
  TEST_ASSERT_EQUAL_UINT32(100, g.percentileMs(99));
  TEST_ASSERT_EQUAL_UINT32(20, g.percentileMs(50));
}

static void test_merge(void) {
  GapStats a, b;
  a.add(5);
  b.add(300);
  b.add(10);
  a.merge(b);
  TEST_ASSERT_EQUAL_UINT32(3, a.count);
  TEST_ASSERT_EQUAL_UINT32(300, a.maxMs);
  TEST_ASSERT_EQUAL_UINT32(2, a.buckets[0]);
  TEST_ASSERT_EQUAL_UINT32(1, a.slowCount());
}

static void test_busy_stats(void) {
  BusyStats n;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, n.busyPct());  // empty window
  n.addIteration(10);
  n.addIteration(1500);
  n.addIteration(0);
  n.windowMs = 60000;
  TEST_ASSERT_EQUAL_UINT32(1510, n.busyMs);
  TEST_ASSERT_EQUAL_UINT32(1500, n.maxBusyMs);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5167f, n.busyPct());
  n.busyMs = 90000;  // can't be busier than the window
  TEST_ASSERT_EQUAL_FLOAT(100.0f, n.busyPct());
}

static PerfReport sampleReport() {
  PerfReport r;
  for (int i = 0; i < 10; ++i) r.ui.add(5);
  r.ui.add(14);
  r.net.addIteration(1840);
  r.net.windowMs = 60000;
  r.uiStackFree = 3120;
  r.netStackFree = 5480;
  r.freeHeap = 201344;
  r.maxBlock = 110580;
  r.uptimeS = 125;
  return r;
}

static void test_log_line(void) {
  const std::string s = perfLogLine(sampleReport());
  TEST_ASSERT_EQUAL_STRING(
      "[perf] ui gap max=14ms p99<20ms slow=0 | net busy 3.1% max=1840ms | stack ui=3120 "
      "net=5480 | heap 201344 (max block 110580)",
      s.c_str());
}

static void test_json(void) {
  const std::string s = perfJson(sampleReport());
  TEST_ASSERT_TRUE(s.find("\"uptime_s\":125") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"gap_max_ms\":14") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"buckets\":[11,0,0,0,0,0]") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"max_busy_ms\":1840") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"stack_free\":{\"ui\":3120,\"net\":5480}") != std::string::npos);
  TEST_ASSERT_EQUAL_CHAR('{', s.front());
  TEST_ASSERT_EQUAL_CHAR('}', s.back());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_gap_stats);
  RUN_TEST(test_gaps_land_in_their_buckets);
  RUN_TEST(test_percentile_from_buckets);
  RUN_TEST(test_percentile_rounds_up);
  RUN_TEST(test_merge);
  RUN_TEST(test_busy_stats);
  RUN_TEST(test_log_line);
  RUN_TEST(test_json);
  return UNITY_END();
}
