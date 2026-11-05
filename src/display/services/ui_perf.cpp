#include "display/services/ui_perf.h"

void UiPerfMeter::setPlannedSleep(uint32_t ms) { plannedSleepMs_ = ms; }

bool UiPerfMeter::tick(uint32_t nowMs) {
  if (lastLoopMs_ != 0) {
    const uint32_t since = nowMs - lastLoopMs_;
    const uint32_t gap = since > plannedSleepMs_ ? since - plannedSleepMs_ : 0;
    window_.add(gap);
    if (gap > share_.maxEverMs) share_.maxEverMs = gap;
    if (gap >= kPerfGapBucketMs[3]) ++share_.slowEver;
  }
  lastLoopMs_ = nowMs;
  if (windowStartMs_ == 0) windowStartMs_ = nowMs;
  if (nowMs - windowStartMs_ < kPerfWindowMs) return false;

  share_.windowCount = window_.count;
  share_.windowMaxMs = window_.maxMs;
  for (int i = 0; i < kPerfGapBuckets; ++i) share_.buckets[i] = window_.buckets[i];
  window_ = GapStats{};
  windowStartMs_ = nowMs;
  return true;
}

const UiPerfShare &UiPerfMeter::share() const { return share_; }

const GapStats &UiPerfMeter::window() const { return window_; }
