#include "admin/domain/device_alerts.h"

#include <algorithm>

#include "admin/app/config_page.h"    // SettingsTab, openSkyDailyRequests()
#include "admin/app/config_portal.h"  // openSkyCreditCost()
#include "display/domain/theme/lcars_theme.h"    // THEME_ALERT / THEME_WARNING

namespace {

// One row per AlertId, in enum order (alertDef() indexes it; a test checks).
const AlertDef kAlertDefs[kAlertCount] = {
    {AlertId::OpenSkyLoginFailed, AlertSeverity::Error, true, SettingsTab::OpenSky,
     Str::AlertLoginTitle, WebStr::AlertLoginTitle, WebStr::AlertLoginAction,
     "opensky_login_failed"},
    {AlertId::OpenSkyQuotaExhausted, AlertSeverity::Error, true, SettingsTab::OpenSky,
     Str::AlertQuotaTitle, WebStr::AlertQuotaTitle, WebStr::AlertQuotaAction,
     "opensky_quota_exhausted"},
    {AlertId::OpenSkyBudgetOverLimit, AlertSeverity::Warning, true, SettingsTab::OpenSky,
     Str::AlertBudgetTitle, WebStr::AlertBudgetTitle, WebStr::AlertBudgetAction,
     "opensky_budget_over_limit"},
};

std::string num(uint32_t v) { return std::to_string(v); }

// Seconds left until the pool renews, counted down from when OpenSky said it.
uint32_t quotaLeftS(const AlertDetails &d, uint32_t nowMs) {
  OpenSkyQuota q;
  q.state = OpenSkyQuota::State::Exhausted;
  q.reset_in_s = d.quotaResetInS;
  q.observed_ms = d.quotaObservedMs;
  return quotaSecondsUntilReset(q, nowMs);
}

}  // namespace

AlertSeverity AlertSet::displaySeverity() const {
  AlertSeverity worst = AlertSeverity::None;
  for (int i = 0; i < kAlertCount; ++i) {
    const AlertId id = static_cast<AlertId>(i);
    if (!has(id) || !alertDef(id).onDisplay) continue;
    worst = std::max(worst, alertDef(id).severity);
  }
  return worst;
}

AlertSet collectAlerts(const AlertInputs &in) {
  AlertSet s;
  if (in.auth.mode == OpenSkyAuthState::Mode::LoginFailed) s.set(AlertId::OpenSkyLoginFailed);
  if (in.quota.state == OpenSkyQuota::State::Exhausted) s.set(AlertId::OpenSkyQuotaExhausted);
  if (in.estimatedDailyCredits > in.dailyLimit) s.set(AlertId::OpenSkyBudgetOverLimit);
  return s;
}

AlertDetails collectAlertDetails(const AlertInputs &in) {
  AlertDetails d;
  if (in.auth.mode == OpenSkyAuthState::Mode::LoginFailed) d.loginHttpCode = in.auth.lastHttpCode;
  if (in.quota.state == OpenSkyQuota::State::Exhausted) {
    d.quotaResetInS = in.quota.reset_in_s;
    d.quotaObservedMs = in.quota.observed_ms;
  }
  d.estimatedDailyCredits = in.estimatedDailyCredits;
  d.dailyLimit = in.dailyLimit;
  d.pollIntervalS = in.pollIntervalS;
  d.minPollIntervalS = minPollIntervalForLimit(in.radiusDeg, in.dailyLimit, in.pausedMinutesPerDay);
  return d;
}

const AlertDef &alertDef(AlertId id) {
  const int i = static_cast<int>(id);
  return kAlertDefs[i >= 0 && i < kAlertCount ? i : 0];
}

std::vector<AlertId> alertsInOrder(AlertSet s, bool displayOnly) {
  std::vector<AlertId> out;
  for (AlertSeverity sev : {AlertSeverity::Error, AlertSeverity::Warning}) {
    for (int i = 0; i < kAlertCount; ++i) {
      const AlertId id = static_cast<AlertId>(i);
      const AlertDef &def = alertDef(id);
      if (s.has(id) && def.severity == sev && (!displayOnly || def.onDisplay)) out.push_back(id);
    }
  }
  return out;
}

uint16_t alertBadgeColor(AlertSeverity s) {
  switch (s) {
    case AlertSeverity::Error:
      return THEME_ALERT;
    case AlertSeverity::Warning:
      return THEME_WARNING;
    case AlertSeverity::None:
    default:
      return LCARS_BLACK;
  }
}

const char *alertWebClass(AlertSeverity s) {
  switch (s) {
    case AlertSeverity::Error:
      return "err";
    case AlertSeverity::Warning:
      return "warn";
    case AlertSeverity::None:
    default:
      return "";
  }
}

std::string alertDisplayBody(AlertId id, const AlertDetails &d, uint32_t nowMs, Language lang) {
  switch (id) {
    case AlertId::OpenSkyLoginFailed:
      return fillTemplate(tr(Str::AlertLoginBody, lang), {num(kOpenSkyAnonymousDailyLimit)});
    case AlertId::OpenSkyQuotaExhausted: {
      if (d.quotaResetInS == 0) return tr(Str::AlertQuotaBodyUnknown, lang);
      const uint32_t left = quotaLeftS(d, nowMs);
      if (left == 0) return tr(Str::AlertQuotaBodySoon, lang);
      return fillTemplate(tr(Str::AlertQuotaBodyIn, lang), {formatWait(left, lang)});
    }
    case AlertId::OpenSkyBudgetOverLimit:
      return fillTemplate(tr(Str::AlertBudgetBody, lang),
                          {num(d.estimatedDailyCredits), num(d.dailyLimit)});
    case AlertId::Count:
    default:
      return "";
  }
}

std::string alertWebWhy(AlertId id, const AlertDetails &d, uint32_t nowMs, Language lang) {
  switch (id) {
    case AlertId::OpenSkyLoginFailed:
      return fillTemplate(webTr(WebStr::AlertLoginWhy, lang),
                          {d.loginHttpCode > 0 ? std::to_string(d.loginHttpCode) : "?",
                           num(kOpenSkyAnonymousDailyLimit), num(kOpenSkyAccountDailyLimit)});
    case AlertId::OpenSkyQuotaExhausted: {
      std::string s = webTr(WebStr::AlertQuotaWhy, lang);
      s += " ";
      const uint32_t left = quotaLeftS(d, nowMs);
      if (d.quotaResetInS == 0) {
        s += webTr(WebStr::QuotaRenewsUnknown, lang);
      } else if (left == 0) {
        s += webTr(WebStr::AlertQuotaWhySoon, lang);
      } else {
        s += fillTemplate(webTr(WebStr::AlertQuotaWhyIn, lang), {formatWait(left, lang)});
      }
      return s;
    }
    case AlertId::OpenSkyBudgetOverLimit:
      return fillTemplate(webTr(WebStr::AlertBudgetWhy, lang),
                          {num(d.pollIntervalS), num(d.estimatedDailyCredits), num(d.dailyLimit),
                           num(d.minPollIntervalS)});
    case AlertId::Count:
    default:
      return "";
  }
}

std::string alertChangeLog(AlertSet before, AlertSet after) {
  std::string out;
  for (int i = 0; i < kAlertCount; ++i) {
    const AlertId id = static_cast<AlertId>(i);
    if (before.has(id) == after.has(id)) continue;
    if (!out.empty()) out += '\n';
    out += after.has(id) ? "[alert] + " : "[alert] - ";
    out += alertDef(id).code;
  }
  return out;
}

uint32_t minPollIntervalForLimit(float radiusDeg, uint32_t limit, uint32_t pausedMinutesPerDay) {
  const uint32_t cost = static_cast<uint32_t>(openSkyCreditCost(radiusDeg));
  const uint32_t requests = limit / cost;  // requests a day that fit
  if (requests == 0) return 0;
  const uint32_t activeS = 86400u - std::min<uint32_t>(pausedMinutesPerDay, 1440u) * 60u;
  if (activeS == 0) return 1;
  // openSkyDailyRequests(): ceil(activeS / p) <= requests  <=>  p >= activeS / requests
  return std::max<uint32_t>((activeS + requests - 1) / requests, 1);
}

std::string fillTemplate(const char *tmpl, const std::vector<std::string> &args) {
  std::string out;
  size_t next = 0;
  for (const char *p = tmpl; *p != '\0'; ++p) {
    if (p[0] == '{' && p[1] == '}') {
      if (next < args.size()) out += args[next];
      ++next;
      ++p;
    } else {
      out += *p;
    }
  }
  return out;
}

AlertDemo alertDemoState() {
  AlertInputs in;
  in.auth.mode = OpenSkyAuthState::Mode::LoginFailed;
  in.auth.lastHttpCode = 401;
  in.quota.state = OpenSkyQuota::State::Exhausted;
  in.quota.reset_in_s = 25012;  // ~6 h 57 min
  in.quota.observed_ms = 0;
  in.radiusDeg = 2.5f;
  in.pollIntervalS = 15;
  in.dailyLimit = openSkyDailyLimit(in.auth);
  in.estimatedDailyCredits = openSkyDailyCredits(in.radiusDeg, in.pollIntervalS, 0);
  AlertDemo demo;
  demo.alerts = collectAlerts(in);
  demo.details = collectAlertDetails(in);
  return demo;
}

#ifdef ARDUINO

#include "display/domain/backlight.h"       // backlightStatus() — tonight's window
#include "admin/domain/config_store.h"    // Config
#include "opensky/domain/opensky_client.h"  // openSkyQuota(), openSkyAuthStatus()

AlertInputs deviceAlertInputs(const Config &cfg) {
  AlertInputs in;
  in.auth = openSkyAuthStatus(cfg);
  in.quota = openSkyQuota();
  in.radiusDeg = cfg.radius_deg;
  in.pollIntervalS = cfg.poll_interval_s;
  in.pausedMinutesPerDay = pausedMinutesPerDay(cfg, backlightStatus(cfg));
  in.dailyLimit = openSkyDailyLimit(in.auth);
  in.estimatedDailyCredits =
      openSkyDailyCredits(in.radiusDeg, in.pollIntervalS, in.pausedMinutesPerDay);
  return in;
}

#endif  // ARDUINO
