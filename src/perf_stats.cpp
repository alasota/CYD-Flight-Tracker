#include "perf_stats.h"

#include <cstdio>

void GapStats::add(uint32_t gapMs) {
  int b = 0;
  while (b < kPerfGapBuckets - 1 && gapMs >= kPerfGapBucketMs[b]) ++b;
  ++buckets[b];
  ++count;
  if (gapMs > maxMs) maxMs = gapMs;
}

uint32_t GapStats::percentileMs(int p) const {
  if (count == 0) return 0;
  // Smallest bucket prefix holding at least p % of the samples (rounded up).
  const uint64_t need = (static_cast<uint64_t>(count) * static_cast<uint64_t>(p) + 99) / 100;
  uint64_t seen = 0;
  for (int b = 0; b < kPerfGapBuckets; ++b) {
    seen += buckets[b];
    if (seen >= need) return b < kPerfGapBuckets - 1 ? kPerfGapBucketMs[b] : maxMs;
  }
  return maxMs;
}

uint32_t GapStats::slowCount() const { return buckets[4] + buckets[5]; }

void GapStats::merge(const GapStats &other) {
  count += other.count;
  if (other.maxMs > maxMs) maxMs = other.maxMs;
  for (int b = 0; b < kPerfGapBuckets; ++b) buckets[b] += other.buckets[b];
}

void BusyStats::addIteration(uint32_t busyMsOfIteration) {
  busyMs += busyMsOfIteration;
  if (busyMsOfIteration > maxBusyMs) maxBusyMs = busyMsOfIteration;
}

float BusyStats::busyPct() const {
  if (windowMs == 0) return 0.0f;
  const float pct = 100.0f * static_cast<float>(busyMs) / static_cast<float>(windowMs);
  return pct > 100.0f ? 100.0f : pct;
}

std::string perfLogLine(const PerfReport &r) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[perf] ui gap max=%ums p99<%ums slow=%u | net busy %.1f%% max=%ums | "
                "stack ui=%u net=%u | heap %u (max block %u) | cache ac=%u rt=%u ap=%u",
                static_cast<unsigned>(r.ui.maxMs), static_cast<unsigned>(r.ui.percentileMs(99)),
                static_cast<unsigned>(r.ui.slowCount()), static_cast<double>(r.net.busyPct()),
                static_cast<unsigned>(r.net.maxBusyMs), static_cast<unsigned>(r.uiStackFree),
                static_cast<unsigned>(r.netStackFree), static_cast<unsigned>(r.freeHeap),
                static_cast<unsigned>(r.maxBlock), static_cast<unsigned>(r.cacheAircraft),
                static_cast<unsigned>(r.cacheRoute), static_cast<unsigned>(r.cacheAirport));
  return buf;
}

std::string perfJson(const PerfReport &r) {
  char buf[640];
  std::snprintf(
      buf, sizeof(buf),
      "{\"uptime_s\":%u,"
      "\"ui\":{\"gap_max_ms\":%u,\"gap_p99_ms\":%u,\"slow\":%u,\"iterations\":%u,"
      "\"buckets\":[%u,%u,%u,%u,%u,%u],\"gap_max_ever_ms\":%u,\"slow_ever\":%u},"
      "\"net\":{\"busy_pct\":%.1f,\"max_busy_ms\":%u,\"max_busy_ever_ms\":%u},"
      "\"stack_free\":{\"ui\":%u,\"net\":%u},"
      "\"heap\":{\"free\":%u,\"max_block\":%u},"
      "\"cache\":{\"ac\":%u,\"rt\":%u,\"ap\":%u}}",
      static_cast<unsigned>(r.uptimeS), static_cast<unsigned>(r.ui.maxMs),
      static_cast<unsigned>(r.ui.percentileMs(99)), static_cast<unsigned>(r.ui.slowCount()),
      static_cast<unsigned>(r.ui.count), static_cast<unsigned>(r.ui.buckets[0]),
      static_cast<unsigned>(r.ui.buckets[1]), static_cast<unsigned>(r.ui.buckets[2]),
      static_cast<unsigned>(r.ui.buckets[3]), static_cast<unsigned>(r.ui.buckets[4]),
      static_cast<unsigned>(r.ui.buckets[5]), static_cast<unsigned>(r.uiMaxEverMs),
      static_cast<unsigned>(r.uiSlowEver), static_cast<double>(r.net.busyPct()),
      static_cast<unsigned>(r.net.maxBusyMs), static_cast<unsigned>(r.netMaxEverMs),
      static_cast<unsigned>(r.uiStackFree), static_cast<unsigned>(r.netStackFree),
      static_cast<unsigned>(r.freeHeap), static_cast<unsigned>(r.maxBlock),
      static_cast<unsigned>(r.cacheAircraft), static_cast<unsigned>(r.cacheRoute),
      static_cast<unsigned>(r.cacheAirport));
  return buf;
}
