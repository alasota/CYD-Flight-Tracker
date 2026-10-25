// perf_stats — how responsive the firmware is (spec/done/04-dual-core.md §5.9).
//
// GapStats: the time between two passes of the UI loop — the "freeze" a user
// feels (a tap that isn't answered, a countdown that skips). BusyStats: how
// long the network task's iterations take, and the longest one (a hint for
// the core 0 task watchdog, 5 s with panic in this arduino-esp32).
//
// Pure: fed with millis() differences by main.cpp / net_task, turned into the
// `[perf]` log line and GET /perf.json. Native-tested.
#pragma once

#include <cstdint>
#include <string>

constexpr int kPerfGapBuckets = 6;
// Upper bounds of the gap buckets: <20, <50, <100, <250, <1000, >=1000 ms.
constexpr uint32_t kPerfGapBucketMs[kPerfGapBuckets - 1] = {20, 50, 100, 250, 1000};
// One log line / one /perf.json window per minute.
constexpr uint32_t kPerfWindowMs = 60000;

struct GapStats {
  uint32_t count = 0;
  uint32_t maxMs = 0;
  uint32_t buckets[kPerfGapBuckets] = {};

  void add(uint32_t gapMs);
  // The bucket upper bound (ms) below which at least p % of the gaps fall;
  // 0 with no samples; the last bucket has no upper bound -> maxMs.
  uint32_t percentileMs(int p) const;
  // Gaps of >= 250 ms — clearly felt as a freeze.
  uint32_t slowCount() const;
  void merge(const GapStats &other);
};

struct BusyStats {
  uint32_t busyMs = 0;    // time spent inside iterations
  uint32_t windowMs = 0;  // wall time the window covers
  uint32_t maxBusyMs = 0;  // the longest single iteration

  void addIteration(uint32_t busyMsOfIteration);
  // Busy share of the window, 0..100 (0 for an empty window).
  float busyPct() const;
};

struct PerfReport {
  GapStats ui;             // this window
  BusyStats net;           // this window
  uint32_t uiMaxEverMs = 0;
  uint32_t uiSlowEver = 0;
  uint32_t netMaxEverMs = 0;
  uint32_t uiStackFree = 0;   // bytes, uxTaskGetStackHighWaterMark
  uint32_t netStackFree = 0;
  uint32_t freeHeap = 0;
  uint32_t maxBlock = 0;
  uint32_t uptimeS = 0;
};

// "[perf] ui gap max=14ms p99<20ms slow=0 | net busy 3.1% max=1840ms |
//  stack ui=3120 net=5480 | heap 201344 (max block 110580)"
std::string perfLogLine(const PerfReport &r);

// GET /perf.json body.
std::string perfJson(const PerfReport &r);
