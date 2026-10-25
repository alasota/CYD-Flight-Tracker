// device_alerts — one way for the device to say "this needs your attention"
// (spec/done/08-device-alerts.md):
//   - display: a "!" in the header on all three screens, red for an error,
//     orange for a warning; tapping it opens a message screen (service_screen);
//   - web panel: a card above the tabs with the same "!", what caused it and
//     a link to the right tab (config_page).
//
// Alerts are *derived* from the current state, never remembered as events:
// one goes away by itself once its cause does (the login works, a poll
// answers 200 again, the interval was changed). Nothing to acknowledge.
//
// The health dot in the header says how the data feed is doing right now;
// the "!" says there's something to fix or to know — even while data flows
// (anonymous after a failed login: green dot, red "!").
//
// A new alert = one row in kAlertDefs[] + one rule in collectAlerts() + its
// texts (i18n.h for the screen, web_i18n.h for the panel, PL and EN). The
// header, the message screen and the panel card draw whatever is listed.
//
// Pure — tested under `pio test -e native`; deviceAlertInputs() at the end
// is the device-only adapter.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "i18n.h"           // Str, Language
#include "opensky_auth.h"   // OpenSkyAuthState
#include "opensky_quota.h"  // OpenSkyQuota
#include "web_i18n.h"       // WebStr

enum class SettingsTab : uint8_t;  // config_page.h (which includes this header)

enum class AlertId : uint8_t {
  OpenSkyLoginFailed,      // the login server rejected client_id / secret
  OpenSkyQuotaExhausted,   // the last /states/all answer was a 429
  OpenSkyBudgetOverLimit,  // the settings use more credits a day than allowed
  Count
};
constexpr int kAlertCount = static_cast<int>(AlertId::Count);
static_assert(kAlertCount <= 32, "AlertSet is a 32-bit mask");

// Ascending: the larger, the more serious (max() = the worst).
enum class AlertSeverity : uint8_t { None, Warning, Error };

struct AlertDef {
  AlertId id;
  AlertSeverity severity;
  bool onDisplay;       // "!" in the header + an entry on the message screen
  SettingsTab tab;      // where the panel's link goes
  Str displayTitle;     // screen: "BŁĄD LOGOWANIA OPENSKY"
  WebStr webTitle;      // panel: "Błąd logowania OpenSky"
  WebStr webAction;     // panel: "Co zrobić: …"
  const char *code;     // stable id for the log: "opensky_login_failed"
};

// The active alerts as a bit mask — POD, cheap to copy and compare in NetState.
struct AlertSet {
  uint32_t bits = 0;
  bool has(AlertId id) const { return (bits >> static_cast<int>(id)) & 1u; }
  void set(AlertId id) { bits |= 1u << static_cast<int>(id); }
  bool any() const { return bits != 0; }
  // The worst of the onDisplay alerts; None = no "!" in the header.
  AlertSeverity displaySeverity() const;
  bool operator==(const AlertSet &o) const { return bits == o.bits; }
  bool operator!=(const AlertSet &o) const { return bits != o.bits; }
};

// The numbers the messages quote. POD, next to AlertSet in NetState.
struct AlertDetails {
  int loginHttpCode = 0;              // the login server's rejection
  uint32_t quotaResetInS = 0;         // until the pool renews, as OpenSky said it…
  uint32_t quotaObservedMs = 0;       //   …at this millis() (like OpenSkyQuota)
  uint32_t estimatedDailyCredits = 0;
  uint32_t dailyLimit = 0;            // 400 / 4000
  uint32_t pollIntervalS = 0;
  uint32_t minPollIntervalS = 0;      // minPollIntervalForLimit()
};

struct AlertInputs {
  OpenSkyAuthState auth;
  OpenSkyQuota quota;
  uint32_t estimatedDailyCredits = 0;  // openSkyDailyCredits(radius, poll, paused)
  uint32_t dailyLimit = 0;             // openSkyDailyLimit(auth)
  float radiusDeg = 0.0f;
  uint32_t pollIntervalS = 0;
  uint32_t pausedMinutesPerDay = 0;
};

// Every rule in one place (spec 08 §2).
AlertSet collectAlerts(const AlertInputs &in);
AlertDetails collectAlertDetails(const AlertInputs &in);

// The row of kAlertDefs[] for `id` (an out-of-range id gets the first row).
const AlertDef &alertDef(AlertId id);
// The active alerts, errors before warnings, then by id; `displayOnly`
// leaves out those that aren't onDisplay.
std::vector<AlertId> alertsInOrder(AlertSet s, bool displayOnly);
// Error -> THEME_ALERT (red), Warning -> THEME_WARNING (orange), None -> black.
uint16_t alertBadgeColor(AlertSeverity s);
// The same colours for the web panel: "err" / "warn" / "".
const char *alertWebClass(AlertSeverity s);

// The one-line text under an entry on the message screen ("Działa
// anonimowo: 400 kredytów/dzień"), isFontSafeText(). `nowMs` counts the
// quota's renewal down from quotaObservedMs.
std::string alertDisplayBody(AlertId id, const AlertDetails &d, uint32_t nowMs, Language lang);
// The panel's "why" paragraph, with the numbers. Plain text (escaped by the page).
std::string alertWebWhy(AlertId id, const AlertDetails &d, uint32_t nowMs, Language lang);
// "[alert] + opensky_login_failed", one line per alert that came or went
// (joined by '\n'); "" when nothing changed.
std::string alertChangeLog(AlertSet before, AlertSet after);

// The shortest poll interval in seconds whose daily credits fit `limit`
// (22 s for 4000 at 1 credit a request, 216 s for 400); 1 when the whole day
// is paused. 0 if not even one request a day fits.
uint32_t minPollIntervalForLimit(float radiusDeg, uint32_t limit, uint32_t pausedMinutesPerDay);

// `tmpl` with each "{}" replaced by the next of `args`, in order (missing
// args -> ""). The alert texts in i18n.h / web_i18n.h use it.
std::string fillTemplate(const char *tmpl, const std::vector<std::string> &args);

// All three alerts with believable numbers — the message screen's
// screenshot (/screenshot.bmp?screen=4) and the panel's (web_alert.png).
struct AlertDemo {
  AlertSet alerts;
  AlertDetails details;
};
AlertDemo alertDemoState();

#ifdef ARDUINO
struct Config;
// The inputs as of now, from opensky_client (auth, quota), the backlight's
// night window and `cfg`. Core 0 (net_task, config_portal).
AlertInputs deviceAlertInputs(const Config &cfg);
#endif
