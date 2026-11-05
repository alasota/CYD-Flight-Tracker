// ui_perf: the UI loop's gaps and the 60 s window handed to net_task. docs/architecture.md "Concurrency — two cores", spec/done/04-dual-core.md.
#include <unity.h>

#include "display/services/ui_perf.h"

#include <cstdint>

#include "test_support.h"

namespace {

// Przerwa pętli UI to czas od poprzedniego przejścia minus zaplanowany sen
static void test_ui_perf_gap_subtracts_planned_sleep(void) {
  UiPerfMeter m;
  TEST_ASSERT_FALSE(m.tick(1000));
  TEST_ASSERT_EQUAL_UINT32(0, m.window().count);  // the first pass after boot counts nothing
  m.setPlannedSleep(20);
  m.tick(1035);  // 35 ms later, 20 of them asleep
  TEST_ASSERT_EQUAL_UINT32(1, m.window().count);
  TEST_ASSERT_EQUAL_UINT32(15, m.window().maxMs);
  TEST_ASSERT_EQUAL_UINT32(15, m.share().maxEverMs);
  m.setPlannedSleep(1000);
  m.tick(1035 + 900);  // woken early (a new NetState): never a negative gap
  TEST_ASSERT_EQUAL_UINT32(2, m.window().count);
  TEST_ASSERT_EQUAL_UINT32(15, m.window().maxMs);
  TEST_ASSERT_EQUAL_UINT32(2, m.window().buckets[0]);  // 15 and 0, both < 20 ms
}

// Przerwa od 250 ms liczy się jako wolne przejście, 249 ms nie
static void test_ui_perf_slow_pass_from_250_ms(void) {
  UiPerfMeter m;
  m.tick(1000);
  m.tick(1000 + 249);
  TEST_ASSERT_EQUAL_UINT32(0, m.share().slowEver);
  m.tick(1000 + 249 + 250);
  TEST_ASSERT_EQUAL_UINT32(1, m.share().slowEver);
  TEST_ASSERT_EQUAL_UINT32(250, m.share().maxEverMs);
  TEST_ASSERT_EQUAL_UINT32(2, m.window().count);
}

// Okno statystyk zamyka się co 60 s i oddaje UiPerfShare rdzeniowi 0
static void test_ui_perf_window_closes_every_60_s(void) {
  for (uint32_t start : {1000u, 0xFFFF0000u, 0xFFFFF000u}) {  // the last one wraps mid-window
    UiPerfMeter m;
    m.setPlannedSleep(20);
    TEST_ASSERT_FALSE(m.tick(start));
    uint32_t t = start;
    uint32_t passes = 0;
    // A pass every 50 ms (30 ms gaps) up to 60 999 ms.
    while (t + 50 - start < kPerfWindowMs) {
      t += 50;
      TEST_ASSERT_FALSE(m.tick(t));
      ++passes;
    }
    TEST_ASSERT_EQUAL_UINT32(start + kPerfWindowMs - 50, t);
    TEST_ASSERT_FALSE(m.tick(start + kPerfWindowMs - 1));  // 60 999 ms: still open
    ++passes;
    m.setPlannedSleep(0);
    TEST_ASSERT_TRUE(m.tick(start + kPerfWindowMs));  // 60 000 ms after the first pass
    ++passes;

    const UiPerfShare &s = m.share();
    TEST_ASSERT_EQUAL_UINT32(passes, s.windowCount);
    TEST_ASSERT_EQUAL_UINT32(30, s.windowMaxMs);
    TEST_ASSERT_EQUAL_UINT32(passes - 1, s.buckets[1]);  // 20..49 ms: the 30 ms gaps, the 29 ms one
    TEST_ASSERT_EQUAL_UINT32(1, s.buckets[0]);           // the last gap: 1 ms
    TEST_ASSERT_EQUAL_UINT32(0, m.window().count);       // the next window starts empty
    TEST_ASSERT_FALSE(m.tick(start + kPerfWindowMs + 30));
    TEST_ASSERT_EQUAL_UINT32(1, m.window().count);
  }
}

// -- edge case: the first pass after boot counts no gap, however late it is
static void test_ui_perf_first_tick_counts_nothing(void) {
  UiPerfMeter m;
  m.setPlannedSleep(0);
  TEST_ASSERT_FALSE(m.tick(5000));
  TEST_ASSERT_EQUAL_UINT32(0, m.window().count);
  TEST_ASSERT_EQUAL_UINT32(0, m.share().maxEverMs);
  TEST_ASSERT_EQUAL_UINT32(0, m.share().slowEver);
  m.tick(5010);
  TEST_ASSERT_EQUAL_UINT32(1, m.window().count);
  TEST_ASSERT_EQUAL_UINT32(10, m.share().maxEverMs);
}

// -- edge case: the totals since boot outlive a window; the window's own numbers don't
static void test_ui_perf_max_ever_survives_window_reset(void) {
  UiPerfMeter m;
  m.tick(1000);
  m.tick(1300);  // a 300 ms gap
  m.setPlannedSleep(kPerfWindowMs);  // the long waits below are planned sleep, no gap
  TEST_ASSERT_TRUE(m.tick(1000 + kPerfWindowMs));
  TEST_ASSERT_EQUAL_UINT32(300, m.share().windowMaxMs);
  TEST_ASSERT_EQUAL_UINT32(2, m.share().windowCount);
  m.setPlannedSleep(0);
  m.tick(1000 + kPerfWindowMs + 10);  // a 10 ms gap
  m.setPlannedSleep(kPerfWindowMs);
  TEST_ASSERT_TRUE(m.tick(1000 + 2 * kPerfWindowMs));
  const UiPerfShare &s = m.share();
  TEST_ASSERT_EQUAL_UINT32(10, s.windowMaxMs);  // this window's own
  TEST_ASSERT_EQUAL_UINT32(2, s.windowCount);
  TEST_ASSERT_EQUAL_UINT32(300, s.maxEverMs);  // since boot
  TEST_ASSERT_EQUAL_UINT32(1, s.slowEver);
}

}  // namespace

void run_ui_perf_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_ui_perf_gap_subtracts_planned_sleep);
  RUN_TEST(test_ui_perf_slow_pass_from_250_ms);
  RUN_TEST(test_ui_perf_window_closes_every_60_s);
  RUN_TEST(test_ui_perf_first_tick_counts_nothing);
  RUN_TEST(test_ui_perf_max_ever_survives_window_reset);
}
