#include "platform/time_sync.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <time.h>

void timeSyncSetZone() {
  setenv("TZ", TZ_WARSAW, 1);
  tzset();
}

void timeSyncBegin() {
  configTzTime(TZ_WARSAW, "pool.ntp.org", "time.nist.gov");
}

bool isTimeSynced() {
  // 2021-01-01 UTC — any value past this means SNTP has run; the ESP32
  // powers up somewhere around 1970.
  return time(nullptr) > 1609459200;
}

std::time_t timeSyncNowLocal() {
  if (!isTimeSynced()) return 0;
  std::time_t now = time(nullptr);
  // Derive the UTC offset (including DST) without relying on the
  // non-standard tm_gmtoff (absent from the ESP32 newlib): read `now` as
  // UTC wall-clock fields, then ask mktime() to treat those same fields
  // as *local* time — the gap between that and `now` is the offset.
  struct tm utc_tm;
  gmtime_r(&now, &utc_tm);
  utc_tm.tm_isdst = -1;
  std::time_t utc_as_local = mktime(&utc_tm);
  std::time_t offset = now - utc_as_local;  // seconds east of UTC, DST-aware
  // Re-encode local wall-clock as if it were UTC so the pure formatters
  // (which break down via UTC) render local time.
  return now + offset;
}

#endif  // ARDUINO
