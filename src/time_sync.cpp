#include "time_sync.h"

#include <cmath>
#include <cstdio>

namespace {

// Civil date (proleptic Gregorian) broken out from an epoch.
struct CivilDateTime {
  int year = 1970;
  int month = 1;        // 1..12
  int day = 1;          // 1..31
  int hour = 0;         // 0..23
  int minute = 0;       // 0..59
  int second = 0;       // 0..59
  int day_of_year = 1;  // 1 == January 1st
};

// days_from_epoch -> (year, month, day), after Howard Hinnant's
// public-domain civil-from-days algorithm. `z` is days since 1970-01-01.
void civilFromDays(long z, int &year, int &month, int &day) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);           // [0, 146096]
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  const int y = static_cast<int>(yoe) + static_cast<int>(era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
  const unsigned mp = (5 * doy + 2) / 153;                       // [0, 11]
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);          // [1, 31]
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);           // [1, 12]
  year = y + (month <= 2 ? 1 : 0);
}

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

CivilDateTime breakdown(std::time_t epoch) {
  CivilDateTime out;
  long secs = static_cast<long>(epoch);
  if (secs < 0) secs = 0;  // pre-1970 unsupported — clamp

  long days = secs / 86400;
  long rem = secs % 86400;
  out.hour = static_cast<int>(rem / 3600);
  out.minute = static_cast<int>((rem % 3600) / 60);
  out.second = static_cast<int>(rem % 60);

  civilFromDays(days, out.year, out.month, out.day);

  static const int kDaysBeforeMonth[12] = {0, 31, 59, 90, 120, 151,
                                           181, 212, 243, 273, 304, 334};
  int doy0 = kDaysBeforeMonth[out.month - 1] + (out.day - 1);
  if (out.month > 2 && isLeapYear(out.year)) doy0 += 1;
  out.day_of_year = doy0 + 1;  // 1 == Jan 1st
  return out;
}

}  // namespace

std::string formatDate(std::time_t epoch) {
  CivilDateTime t = breakdown(epoch);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d", t.day, t.month, t.year);
  return std::string(buf);
}

std::string formatTime(std::time_t epoch) {
  CivilDateTime t = breakdown(epoch);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
  return std::string(buf);
}

std::string computeStardate(std::time_t epoch) {
  CivilDateTime t = breakdown(epoch);
  double stardate =
      static_cast<double>(t.year - 2323) * 1000.0 + static_cast<double>(t.day_of_year) * 2.7378;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", stardate);
  return std::string(buf);
}

#ifdef ARDUINO

#include <Arduino.h>
#include <time.h>

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
