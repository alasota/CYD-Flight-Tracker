#include "opensky_quota.h"

#include <cstdio>

bool parseQuotaHeader(const std::string &value, uint32_t *out) {
  if (value.empty()) return false;
  uint64_t v = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
    if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull;  // saturate, keep scanning for bad chars
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

OpenSkyQuota updateQuota(const OpenSkyQuota &previous, int httpCode,
                         const std::string &remainingHeader, const std::string &retryAfterHeader,
                         uint32_t now_ms) {
  OpenSkyQuota q = previous;
  if (httpCode == 200) {
    uint32_t remaining;
    if (parseQuotaHeader(remainingHeader, &remaining)) {
      q.state = OpenSkyQuota::State::Available;
      q.remaining = remaining;
      q.reset_in_s = 0;
      q.observed_ms = now_ms;
    } else if (q.state == OpenSkyQuota::State::Exhausted) {
      q.state = OpenSkyQuota::State::Unknown;  // answered, so not exhausted any more
      q.reset_in_s = 0;
    }
  } else if (httpCode == 429) {
    uint32_t wait = 0;
    q.state = OpenSkyQuota::State::Exhausted;
    q.remaining = 0;
    q.reset_in_s = parseQuotaHeader(retryAfterHeader, &wait) ? wait : 0;
    q.observed_ms = now_ms;
  }
  return q;
}

uint32_t quotaSecondsUntilReset(const OpenSkyQuota &q, uint32_t now_ms) {
  if (q.state != OpenSkyQuota::State::Exhausted || q.reset_in_s == 0) return 0;
  const uint32_t elapsed_s = (now_ms - q.observed_ms) / 1000u;  // unsigned: wrap-safe
  return elapsed_s >= q.reset_in_s ? 0 : q.reset_in_s - elapsed_s;
}

std::string formatWait(uint32_t seconds, Language lang) {
  if (seconds < 60) return "< 1 min";
  const uint32_t minutes = (seconds + 30) / 60;  // nearest minute
  const uint32_t h = minutes / 60;
  const uint32_t m = minutes % 60;
  const char *hours = lang == Language::PL ? "godz." : "h";
  char buf[32];
  if (h == 0) {
    std::snprintf(buf, sizeof(buf), "%u min", static_cast<unsigned>(m));
  } else if (m == 0) {
    std::snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(h), hours);
  } else {
    std::snprintf(buf, sizeof(buf), "%u %s %u min", static_cast<unsigned>(h), hours,
                  static_cast<unsigned>(m));
  }
  return buf;
}
