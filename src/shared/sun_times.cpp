#include "shared/sun_times.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

int wrapMinutes(int m) {
  m %= 1440;
  return m < 0 ? m + 1440 : m;
}

// Julian date (fractional) -> minutes after that date's UTC midnight.
int julianToUtcMinutes(double jd) {
  const double dayFraction = jd + 0.5 - std::floor(jd + 0.5);
  return wrapMinutes(static_cast<int>(std::lround(dayFraction * 1440.0)));
}

}  // namespace

SunTimes computeSunTimes(int year, int month, int day, float lat_deg, float lon_deg) {
  // Julian date at 0h UT (Fliegel-style, valid 1901..2099), then at noon.
  const double jd0 = 367.0 * year - std::floor(7.0 * (year + std::floor((month + 9) / 12.0)) / 4.0) +
                     std::floor(275.0 * month / 9.0) + day + 1721013.5;
  const double n = std::round(jd0 + 0.5 - 2451545.0 + 0.0008);
  const double jStar = n - lon_deg / 360.0;  // mean solar noon
  const double m = std::fmod(357.5291 + 0.98560028 * jStar, 360.0);
  const double c = 1.9148 * std::sin(m * kDeg) + 0.0200 * std::sin(2 * m * kDeg) +
                   0.0003 * std::sin(3 * m * kDeg);
  const double lambda = std::fmod(m + c + 180.0 + 102.9372, 360.0);
  const double transit =
      2451545.0 + jStar + 0.0053 * std::sin(m * kDeg) - 0.0069 * std::sin(2 * lambda * kDeg);
  const double sinDecl = std::sin(lambda * kDeg) * std::sin(23.4397 * kDeg);
  const double cosDecl = std::cos(std::asin(sinDecl));
  const double cosOmega = (std::sin(-0.833 * kDeg) - std::sin(lat_deg * kDeg) * sinDecl) /
                          (std::cos(lat_deg * kDeg) * cosDecl);

  SunTimes s;
  if (cosOmega > 1.0) {
    s.kind = SunTimes::Kind::PolarNight;
    return s;
  }
  if (cosOmega < -1.0) {
    s.kind = SunTimes::Kind::PolarDay;
    return s;
  }
  const double omegaDays = std::acos(cosOmega) / kDeg / 360.0;
  s.sunrise_min = julianToUtcMinutes(transit - omegaDays);
  s.sunset_min = julianToUtcMinutes(transit + omegaDays);
  return s;
}

SunTimes shiftSunTimes(const SunTimes &utc, int offset_min) {
  SunTimes s = utc;
  s.sunrise_min = wrapMinutes(utc.sunrise_min + offset_min);
  s.sunset_min = wrapMinutes(utc.sunset_min + offset_min);
  return s;
}
