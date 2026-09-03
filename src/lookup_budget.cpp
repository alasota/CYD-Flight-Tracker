#include "lookup_budget.h"

#include <algorithm>

LookupPlan planLookups(const std::vector<LookupKeys> &byPriority,
                       const std::function<bool(const std::string &)> &aircraftCached,
                       const std::function<bool(const std::string &)> &routeCached, int budget) {
  LookupPlan plan;
  int newFetches = 0;

  auto already = [](const std::vector<std::string> &v, const std::string &key) {
    return std::find(v.begin(), v.end(), key) != v.end();
  };

  auto consider = [&](const std::string &key, std::vector<std::string> &out,
                      const std::function<bool(const std::string &)> &cached) {
    if (key.empty() || already(out, key)) {
      return;
    }
    if (cached(key)) {
      out.push_back(key);  // cache hit — free
      return;
    }
    if (newFetches < budget) {
      out.push_back(key);
      ++newFetches;
    }
    // else: over budget this cycle — leave it for a later poll.
  };

  for (const LookupKeys &keys : byPriority) {
    consider(keys.icao24, plan.icao24, aircraftCached);
    consider(keys.callsign, plan.callsigns, routeCached);
  }

  return plan;
}
