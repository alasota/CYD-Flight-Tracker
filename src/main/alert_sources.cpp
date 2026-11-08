// alert_sources — where the device-alert inputs come from on the board: the
// OpenSky login and credits (opensky_client), tonight's night window
// (backlight) and the settings. Joins data from three contexts, so it lives in
// the composition root; not covered by Unity (CLAUDE.md "Testing"),
// collectAlerts() and the rest of device_alerts are pure.
#include "admin/core/device_alerts.h"

#include "admin/core/config_page.h"       // openSkyDailyCredits(), pausedMinutesPerDay()
#include "admin/core/settings.h"      // Settings
#include "display/core/backlight.h"       // backlightStatus() — tonight's window
#include "opensky/core/opensky_client.h"  // openSkyQuota(), openSkyAuthStatus()

AlertInputs deviceAlertInputs(const Settings &cfg) {
  AlertInputs in;
  in.auth = openSkyAuthStatus(cfg.opensky);
  in.quota = openSkyQuota();
  in.radiusDeg = cfg.home.radius_deg;
  in.pollIntervalS = cfg.poll.interval_s;
  in.pausedMinutesPerDay = pausedMinutesPerDay(cfg.backlight, backlightStatus(cfg.backlight));
  in.dailyLimit = openSkyDailyLimit(in.auth);
  in.estimatedDailyCredits =
      openSkyDailyCredits(in.radiusDeg, in.pollIntervalS, in.pausedMinutesPerDay);
  return in;
}
