// config_page — the HTML of the config web page (served by config_portal),
// rendered as a stream of chunks so the ESP32 never builds the whole page in
// RAM. Five tabs (Location / OpenSky client / General / Backup / Firmware), PL or EN text from
// web_i18n (Config::web_language), dark theme with the display's peach as
// the single accent colour.
//
// Pure — no WebServer / Arduino dependency: the caller supplies the sink
// (config_portal sends each chunk over HTTP, the native tests collect them
// into a string). Tested under `pio test -e native`.
#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>

#include "config_store.h"   // Config
#include "device_alerts.h"  // AlertSet, AlertDetails
#include "opensky_auth.h"   // OpenSkyAuthState
#include "opensky_quota.h"  // OpenSkyQuota
#include "release_check.h"  // UpdateState
#include "web_i18n.h"       // WebStr

enum class SettingsTab : uint8_t { Location, OpenSky, General, Backup, Firmware };
constexpr int kSettingsTabCount = 5;

// "location" / "opensky" / "general" / "backup" / "firmware" — the ?tab= / hidden-field value.
const char *settingsTabId(SettingsTab tab);
// Inverse of settingsTabId(); anything unknown -> Location.
SettingsTab settingsTabFromArg(const std::string &arg);
// The tab's name on the tab bar (and in the alert card's "Go to:" link).
WebStr settingsTabLabel(SettingsTab tab);

// True when both OpenSky OAuth fields are set (else the firmware polls
// anonymously).
bool hasOpenSkyCredentials(const Config &cfg);

// Rough OpenSky credit use per day if the display polls non-stop except for
// `paused_min_per_day` minutes (night pause, spec/03):
// ceil((86400 - 60 * paused) / poll_interval_s) requests x openSkyCreditCost(radius).
// poll_interval_s 0 is treated as 1.
uint32_t openSkyDailyRequests(uint32_t poll_interval_s, uint32_t paused_min_per_day = 0);
uint32_t openSkyDailyCredits(float radius_deg, uint32_t poll_interval_s,
                             uint32_t paused_min_per_day = 0);

// Minutes of tonight's night window (by the clock, or today's sunset ->
// sunrise from `light`); 0 when the night mode is off or the sun unknown.
uint32_t nightMinutesToday(const Config &cfg, const BacklightStatus &light);
// Minutes a day OpenSky isn't polled: the night window, if pausing is on.
uint32_t pausedMinutesPerDay(const Config &cfg, const BacklightStatus &light);

// The General tab's "Now: ..." state word(s): day / night / screen off /
// woken by a touch / clock not synced, + "fetching paused" when it is.
std::string lightStateText(const BacklightStatus &s, Language lang);

// &, <, >, " escaped — for user-entered values put into attributes.
std::string htmlEscape(const std::string &in);

struct ConfigPageModel {
  Config cfg;
  SettingsTab tab = SettingsTab::Location;  // tab shown on load
  bool hasMessage = false;
  WebStr message = WebStr::Saved;
  bool isError = false;
  std::string messageDetail;  // appended after the message (escaped on output)
  // OpenSky credits, from opensky_client's last answer; quotaResetInS is the
  // time left until an exhausted pool renews, already counted down to "now"
  // by the caller (quotaSecondsUntilReset()).
  OpenSkyQuota quota;
  uint32_t quotaResetInS = 0;
  // A firmware update just succeeded and the board is about to restart: the
  // page says so and reloads itself once the board answers again.
  bool restarting = false;
  // Updates from GitHub (release_check), for the Firmware tab's first card.
  UpdateState update;
  // The version shown as installed; "" = firmwareVersion(). (The demo
  // screenshot renders a made-up one.)
  std::string installedVersion;
  // Light sensor / backlight right now (General tab; refreshed live from
  // GET /light.json by the page's script).
  BacklightStatus light;
  // Device alerts (spec 08): a card above the tabs when any is active —
  // computed with the same collectAlerts() as the display's "!". `nowMs`
  // counts the quota's renewal down (alertWebWhy()).
  AlertSet alerts;
  AlertDetails alertDetails;
  uint32_t nowMs = 0;
  // OpenSky login state (spec 06), from openSkyAuthStatus(): the OpenSky tab's
  // pill, the "login failed" warning and the daily limit. The page uses it as
  // onCredentials(auth, <cfg's credentials>) — so a default model with
  // credentials in cfg reads as Account, and just-saved other credentials too.
  OpenSkyAuthState auth;
  // Wall clock now, already in local time (formatDate()/formatTime()); 0 =
  // clock not synced -> the warning leaves out "since".
  std::time_t nowEpoch = 0;
};

// The OpenSky tab's warning in LoginFailed, plain text (no HTML), in `lang`:
// "OpenSky rejected ... (HTTP 401, since 25.10.2026 14:32). ... a limit of 400
// credits a day instead of 4000. ... Next login attempt in 27 min." — "since"
// only when nowEpoch != 0; minutes rounded up from nextTokenTryMs - nowMs
// (wrap-safe), at least 1.
std::string loginFailedDetailText(const OpenSkyAuthState &s, uint32_t nowMs,
                                  std::time_t nowEpoch, Language lang);

// Where the Firmware tab sends people for new versions.
constexpr const char *kReleasesUrl = "https://github.com/alasota/CYD-Flight-Tracker/releases/latest";

// The OpenSky tab's "credits left" line as plain text (no HTML) in `lang`:
//   Unknown    -> "Credits left: not known yet, ..."
//   Available  -> "Credits left today: 1234 (as of the last request)"
//   Exhausted  -> "Daily limit reached: renews in about 6 h 57 min." / "...should have
//                 renewed by now..." / "...OpenSky didn't say when it renews."
// `exhausted` is set for the last case (the page shows it as a warning).
std::string quotaStatusText(const OpenSkyQuota &q, uint32_t resetInS, Language lang,
                            bool *exhausted);

using HtmlSink = std::function<void(const char *chunk)>;

// Emits the whole page, in order, as a series of chunks (never an empty one).
void renderConfigPage(const ConfigPageModel &model, const HtmlSink &emit);
