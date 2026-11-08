// net_perf — the network task's share of /perf.json and the [perf] log line:
// how long each pass of net_task took, reported once a minute together with
// the UI's numbers (perf_stats, spec/04 §5.9).
//
// Device-only (net_task's stack and FreeRTOS). perf_stats holds the logic.
#pragma once

#include <cstdint>
#include <string>

// One pass of the network task took `busyMs`; closes the window when due.
void perfIteration(uint32_t busyMs);

// The body of GET /perf.json, built on the net task (it measures its stack).
std::string perfJsonNow();
