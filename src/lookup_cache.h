// lookup_cache — a size-limited (LRU) RAM cache for the hexdb.io lookups
// (spec/done/05-bounded-lookup-cache.md). Before it, aircraft_lookup and
// route_lookup kept every answer since boot in a plain std::map: a few hundred
// new aircraft a day over Warsaw, so after a few days the heap shrank and
// release_check quietly stopped (largest block < 50 KB).
//
// RAM doesn't need everything: every confirmed answer is also in NVS
// (lookup_store), and an NVS read costs no lookup budget. An entry that falls
// out of RAM comes back from NVS, not from hexdb.io.
//
// Pure, header-only, native-tested (test_enrichment/lookup_cache_test.cpp).
// Not thread-safe: all three caches live on core 0 (net_task).
#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>

// Capacities. Each must hold at least one poll's working set (every aircraft
// in range — usually 10-40 at ±2.5°), or every poll would read NVS again.
// Worst case ~250 B per entry -> ~64 KB for all three; usually far less (short
// strings fit SSO).
constexpr size_t kAircraftCacheCapacity = 96;  // key icao24
constexpr size_t kRouteCacheCapacity = 96;     // key callsign
constexpr size_t kAirportCacheCapacity = 64;   // key ICAO airport code

template <typename V>
class LookupCache {
 public:
  explicit LookupCache(size_t capacity) : capacity_(capacity) {}

  // A hit moves the entry to the front (most recently used). The pointer is
  // valid until the next put() / clear().
  const V *find(const std::string &key) {
    auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    entries_.splice(entries_.begin(), entries_, it->second);  // no allocation
    return &it->second->second;
  }

  // Doesn't change the order: net_task asks about every aircraft on every
  // poll (budget planning), and a question isn't a use.
  bool contains(const std::string &key) const { return index_.count(key) > 0; }

  // Overwrites or adds; when full, drops the least recently used entry.
  void put(const std::string &key, V value) {
    if (capacity_ == 0) return;
    auto it = index_.find(key);
    if (it != index_.end()) {
      it->second->second = std::move(value);
      entries_.splice(entries_.begin(), entries_, it->second);
      return;
    }
    if (entries_.size() >= capacity_) {
      index_.erase(entries_.back().first);
      entries_.pop_back();
    }
    entries_.emplace_front(key, std::move(value));
    index_[key] = entries_.begin();
  }

  size_t size() const { return entries_.size(); }
  size_t capacity() const { return capacity_; }

  void clear() {
    index_.clear();
    entries_.clear();
  }

 private:
  using Entries = std::list<std::pair<std::string, V>>;
  size_t capacity_;
  Entries entries_;  // front = most recently used
  std::unordered_map<std::string, typename Entries::iterator> index_;
};
