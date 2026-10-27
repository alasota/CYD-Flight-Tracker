// opensky_quota — how many OpenSky API credits are left today, from the
// headers OpenSky puts on every /states/all answer (docs/data-sources.md "Rate limits"):
//   200 + X-Rate-Limit-Remaining: <n>            -> n credits left
//   429 + X-Rate-Limit-Retry-After-Seconds: <s>  -> exhausted, pool renews in s
// opensky_client keeps the latest state (openSkyQuota()); it shows up in the
// serial log and on the config page. Costs no extra requests.
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <cstdint>
#include <string>

#include "display/domain/i18n.h"  // Language

struct OpenSkyQuota {
  enum class State : uint8_t {
    Unknown,    // no answer with a credit header yet (or after the pool renewed)
    Available,  // `remaining` credits left, as of the last request
    Exhausted,  // 429 — `reset_in_s` until the pool renews, counted from `observed_ms`
  };
  State state = State::Unknown;
  uint32_t remaining = 0;
  uint32_t reset_in_s = 0;   // 0 = OpenSky didn't say
  uint32_t observed_ms = 0;  // millis() of the answer that set this state
};

// A credit / seconds header value: digits only (no sign, no spaces).
// Values past uint32 saturate. False for empty or anything else.
bool parseQuotaHeader(const std::string &value, uint32_t *out);

// The quota after one /states/all answer with HTTP status `httpCode`:
// 200 with a valid remaining-credits header -> Available; 200 without one
// keeps a known count but turns Exhausted into Unknown (the pool is clearly
// back); 429 -> Exhausted (+ retry-after if valid); anything else -> unchanged.
OpenSkyQuota updateQuota(const OpenSkyQuota &previous, int httpCode,
                         const std::string &remainingHeader, const std::string &retryAfterHeader,
                         uint32_t now_ms);

// Seconds until an Exhausted pool renews, as of `now_ms` (0 if unknown,
// already due, or not exhausted). Wrap-safe.
uint32_t quotaSecondsUntilReset(const OpenSkyQuota &q, uint32_t now_ms);

// A short wait for people: "6 h 57 min", "2 h", "45 min", "< 1 min"
// (PL: "6 godz. 57 min", "2 godz.", "45 min", "< 1 min").
std::string formatWait(uint32_t seconds, Language lang);
