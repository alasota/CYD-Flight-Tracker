// ui_perf — the UI loop's gaps for /perf.json (perf_stats, spec/done/04-dual-core.md):
// the gap of a pass is the time since the previous pass minus the sleep that
// pass asked for (its drawing time plus any oversleep, never negative). Gaps
// go into a kPerfWindowMs window; when it closes, share() holds the window
// and the totals since boot for net_task (core_link::uiPerf).
//
// Pure — tested under `pio test -e native`; main.cpp adds the stack high-water
// mark and hands share() over.
#pragma once

#include <cstdint>

#include "platform/core_link.h"   // UiPerfShare
#include "platform/perf_stats.h"  // GapStats, kPerfWindowMs, kPerfGapBucketMs

class UiPerfMeter {
 public:
  // What this pass asks to sleep (uiSleepMs()) — subtracted from the next gap.
  void setPlannedSleep(uint32_t ms);
  // At the start of a pass. The first pass after boot counts no gap. True =
  // the window closed (kPerfWindowMs after it started): share() has it and a
  // new window starts empty.
  bool tick(uint32_t nowMs);
  const UiPerfShare &share() const;
  // The window still open.
  const GapStats &window() const;

 private:
  GapStats window_;
  UiPerfShare share_;
  uint32_t windowStartMs_ = 0;  // 0 = not started
  uint32_t lastLoopMs_ = 0;     // 0 = no pass yet
  uint32_t plannedSleepMs_ = 0;
};
