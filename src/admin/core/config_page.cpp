#include "admin/core/config_page.h"

#include "admin/core/config_page_tabs.h"
#include "admin/core/config_page_writer.h"

#include <algorithm>
#include <vector>

#include "opensky/core/scan_area.h"  // openSkyCreditCost()
#include "shared/clock.h"            // formatDate() / formatTime() — "checked" time
#include "admin/core/firmware/version.h"        // firmwareVersion() — page footer
#include "admin/core/web_assets.h"  // kWebStyleCss, kWebAppJs, ... — src/admin/assets/

const char *settingsTabId(SettingsTab tab) {
  switch (tab) {
    case SettingsTab::OpenSky:
      return "opensky";
    case SettingsTab::General:
      return "general";
    case SettingsTab::Backup:
      return "backup";
    case SettingsTab::Firmware:
      return "firmware";
    case SettingsTab::Location:
    default:
      return "location";
  }
}

SettingsTab settingsTabFromArg(const std::string &arg) {
  if (arg == "opensky") return SettingsTab::OpenSky;
  if (arg == "general") return SettingsTab::General;
  if (arg == "backup") return SettingsTab::Backup;
  if (arg == "firmware") return SettingsTab::Firmware;
  return SettingsTab::Location;
}

WebStr settingsTabLabel(SettingsTab tab) {
  switch (tab) {
    case SettingsTab::OpenSky:
      return WebStr::TabOpenSky;
    case SettingsTab::General:
      return WebStr::TabGeneral;
    case SettingsTab::Backup:
      return WebStr::TabBackup;
    case SettingsTab::Firmware:
      return WebStr::TabFirmware;
    case SettingsTab::Location:
    default:
      return WebStr::TabLocation;
  }
}

bool hasOpenSkyCredentials(const Settings &cfg) {
  return !cfg.opensky.client_id.empty() && !cfg.opensky.client_secret.empty();
}

uint32_t openSkyDailyRequests(uint32_t poll_interval_s, uint32_t paused_min_per_day) {
  const uint32_t poll = poll_interval_s == 0 ? 1 : poll_interval_s;
  const uint32_t pausedS = std::min<uint32_t>(paused_min_per_day, 1440u) * 60u;
  return (86400u - pausedS + poll - 1) / poll;
}

uint32_t openSkyDailyCredits(float radius_deg, uint32_t poll_interval_s,
                             uint32_t paused_min_per_day) {
  return openSkyDailyRequests(poll_interval_s, paused_min_per_day) *
         static_cast<uint32_t>(openSkyCreditCost(radius_deg));
}

uint32_t nightMinutesToday(const BacklightSettings &backlight, const BacklightStatus &light) {
  const NightWindow w = nightWindowFor(backlight.night_mode, backlight.night_from_min,
                                       backlight.night_to_min, light.sun_local, light.sun_known);
  return static_cast<uint32_t>(nightWindowMinutes(w));
}

uint32_t pausedMinutesPerDay(const BacklightSettings &backlight, const BacklightStatus &light) {
  return backlight.night_pause_polling ? nightMinutesToday(backlight, light) : 0;
}

std::string lightStateText(const BacklightStatus &s, Language lang) {
  WebStr id = WebStr::LightDay;
  if (!s.synced) {
    id = WebStr::LightNotSynced;
  } else if (s.screen_off) {
    id = WebStr::LightScreenOff;
  } else if (s.woken) {
    id = WebStr::LightWoken;
  } else if (s.night) {
    id = WebStr::LightNight;
  }
  std::string out = webTr(id, lang);
  if (!s.polling) out += std::string(" \xC2\xB7 ") + webTr(WebStr::LightPollingPaused, lang);
  return out;
}

std::string loginFailedDetailText(const OpenSkyAuthState &s, uint32_t nowMs,
                                  std::time_t nowEpoch, Language lang) {
  // "since": the first rejection's millis() turned into wall-clock time —
  // only with a synced clock (an unsynced one would say 1970) and while the
  // age is trustworthy (millis() wraps after ~49.7 days: a wrapped age would
  // give a date weeks too late, so it is left out instead).
  std::string since;
  if (nowEpoch != 0 && loginFailedSinceKnown(s, nowMs)) {
    const uint32_t agoS = (nowMs - s.failedSinceMs) / 1000u;  // unsigned: wrap-safe
    const std::time_t at = nowEpoch - static_cast<std::time_t>(agoS);
    since = fillTemplate(webTr(WebStr::LoginFailedSince, lang),
                         {formatDate(at) + " " + formatTime(at)});
  }
  // Minutes to the next login attempt, rounded up; "1 min" once it is due
  // (the poll that retries just hasn't run yet), never 0 or negative.
  const int32_t leftMs = static_cast<int32_t>(s.nextTokenTryMs - nowMs);
  uint32_t minutes = 1;
  if (leftMs > 0) minutes = std::max<uint32_t>(1u, (static_cast<uint32_t>(leftMs) + 59999u) / 60000u);
  return fillTemplate(webTr(WebStr::LoginFailedDetail, lang),
                      {std::to_string(s.lastHttpCode), since,
                       std::to_string(kOpenSkyAnonymousDailyLimit),
                       std::to_string(kOpenSkyAccountDailyLimit), std::to_string(minutes)});
}

std::string quotaStatusText(const OpenSkyQuota &q, uint32_t resetInS, Language lang,
                            bool *exhausted) {
  *exhausted = false;
  std::string s;
  switch (q.state) {
    case OpenSkyQuota::State::Available: {
      char n[16];
      std::snprintf(n, sizeof(n), "%u", static_cast<unsigned>(q.remaining));
      s = std::string(webTr(WebStr::QuotaLeftToday, lang)) + " " + n + " " +
          webTr(WebStr::QuotaFromLastRequest, lang);
      break;
    }
    case OpenSkyQuota::State::Exhausted:
      *exhausted = true;
      s = std::string(webTr(WebStr::QuotaExhausted, lang)) + ": ";
      if (q.reset_in_s == 0) {
        s += webTr(WebStr::QuotaRenewsUnknown, lang);
      } else if (resetInS == 0) {
        s += webTr(WebStr::QuotaRenewsSoon, lang);
      } else {
        s += std::string(webTr(WebStr::QuotaRenewsIn, lang)) + " " + formatWait(resetInS, lang) + ".";
      }
      break;
    case OpenSkyQuota::State::Unknown:
    default:
      s = webTr(WebStr::QuotaUnknown, lang);
      break;
  }
  return s;
}

std::string htmlEscape(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

namespace {

using namespace config_page_detail;

// Above the tabs, on every tab: what the display's "!" means and each active
// alert with why, what to do and a link to its tab (spec 08 §3.6).
void alertCard(Writer &w, const ConfigPageModel &m, Language lang) {
  const std::vector<AlertId> ids = alertsInOrder(m.alerts, /*displayOnly=*/false);
  if (ids.empty()) return;
  AlertSeverity worst = AlertSeverity::None;
  for (AlertId id : ids) worst = std::max(worst, alertDef(id).severity);
  const std::string worstClass = alertWebClass(worst);
  w.raw("<div class=\"card alert " + worstClass + "\" id=\"alerts\"><div class=\"ah\">"
        "<span class=\"bang " + worstClass + "\">!</span><div><b>");
  w.t(WebStr::AlertCardTitle);
  w.raw("</b><div class=\"hint\">");
  w.t(WebStr::AlertCardIntro);
  w.raw("</div></div></div>");
  for (AlertId id : ids) {
    const AlertDef &def = alertDef(id);
    const std::string cls = alertWebClass(def.severity);
    w.raw("<div class=\"ai " + cls + "\" id=\"alert-" + def.code + "\"><span class=\"bang " +
          cls + "\">!</span><div><b>");
    w.t(def.webTitle);
    w.raw("</b><p>");
    w.esc(alertWebWhy(id, m.alertDetails, m.nowMs, lang));
    w.raw("</p><p>");
    w.t(def.webAction);
    w.raw("</p><a class=\"go\" href=\"/?tab=");
    w.raw(settingsTabId(def.tab));
    w.raw("\">→ ");
    w.t(WebStr::AlertGoTo);
    w.raw(" ");
    w.t(settingsTabLabel(def.tab));
    w.raw("</a></div></div>");
  }
  w.raw("</div>");
}

// <form id=ID ... action=/update/install><input type=hidden name=tag value=TAG></form>
void installForm(Writer &w, const char *id, const std::string &tag) {
  w.raw("<form id=\"");
  w.raw(id);
  w.raw("\" method=\"POST\" action=\"/update/install\"><input type=\"hidden\" name=\"tag\" "
        "value=\"");
  w.esc(tag);
  w.raw("\"></form>");
}

}  // namespace

void renderConfigPage(const ConfigPageModel &m, const HtmlSink &emit) {
  const Settings &c = m.cfg;
  const Language lang = c.web.language;
  Writer w(emit, lang);
  const std::string installed = m.installedVersion.empty() ? firmwareVersion() : m.installedVersion;

  // ---- head + header
  w.raw("<!DOCTYPE html><html lang=\"");
  w.raw(languageCode(lang));
  w.raw("\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  w.t(WebStr::PageTitle);
  // The project page's icon (site/favicon.svg) as a data URI, so the browser
  // doesn't ask the device for /favicon.ico; the style from style.css.
  w.raw("</title><link rel=\"icon\" type=\"image/svg+xml\" href=\"");
  w.raw(kWebFaviconUri);
  w.raw("\"><style>");
  w.raw(kWebStyleCss);
  w.raw("</style></head><body><div class=\"wrap\"><header><div><h1>Sky Tracker</h1><div class=\"sub\">");
  w.t(WebStr::Subtitle);
  w.raw(" · cyd-sky.local</div></div><nav class=\"lang\">");
  // PL/EN switch: two small POST forms (spec 10 §2.2 — no state change via
  // GET), each back to the tab shown (show() keeps the hidden `tab` current).
  for (Language l : {Language::PL, Language::EN}) {
    w.raw("<form method=\"post\" action=\"/lang\"><input type=\"hidden\" name=\"l\" value=\"");
    w.raw(languageCode(l));
    w.raw("\"><input type=\"hidden\" name=\"tab\" value=\"");
    w.raw(settingsTabId(m.tab));
    w.raw(l == lang ? "\"><button class=\"on\">" : "\"><button>");
    w.raw(l == Language::PL ? "PL" : "EN");
    w.raw("</button></form>");
  }
  w.raw("</nav></header>");

  if (m.hasMessage) {
    w.raw(m.isError ? "<div class=\"msg err\">" : "<div class=\"msg ok\">");
    w.t(m.message);
    if (!m.messageDetail.empty()) {
      w.raw(" ");
      w.esc(m.messageDetail);
    }
    if (m.restarting) {
      w.raw("<br>");
      w.t(WebStr::FirmwareRestarting);
    }
    w.raw("</div>");
  }

  alertCard(w, m, lang);

  // ---- tab bar
  w.raw("<div class=\"tabs\" role=\"tablist\">");
  const SettingsTab tabs[kSettingsTabCount] = {SettingsTab::Location, SettingsTab::OpenSky,
                                               SettingsTab::General, SettingsTab::Backup,
                                               SettingsTab::Firmware};
  for (int i = 0; i < kSettingsTabCount; ++i) {
    w.raw("<button type=\"button\" data-t=\"");
    w.raw(settingsTabId(tabs[i]));
    w.raw(tabs[i] == m.tab ? "\" class=\"on\">" : "\">");
    w.t(settingsTabLabel(tabs[i]));
    w.raw("</button>");
  }
  w.raw("</div>");

  w.raw("<form id=\"s\" method=\"POST\" action=\"/save\">"
        "<input type=\"hidden\" name=\"tab\" id=\"tab\" value=\"");
  w.raw(settingsTabId(m.tab));
  w.raw("\">");

  locationTab(w, m);
  openSkyTab(w, m, lang);
  generalTab(w, m, lang);
  backupTab(w, m);
  firmwareTab(w, m, installed);

  // ---- save, side forms, footer, script
  w.raw(m.tab == SettingsTab::Backup || m.tab == SettingsTab::Firmware
            ? "<button type=\"submit\" class=\"save\" id=\"save\" hidden>"
            : "<button type=\"submit\" class=\"save\" id=\"save\">");
  w.t(WebStr::Save);
  w.raw("</button></form>"
        "<form id=\"up\" method=\"POST\" action=\"/upload_credentials\" "
        "enctype=\"multipart/form-data\"></form>"
        "<form id=\"bk\" method=\"GET\" action=\"/backup.json\"></form>"
        "<form id=\"rs\" method=\"POST\" action=\"/restore\" "
        "enctype=\"multipart/form-data\"></form>"
        "<form id=\"fw\" method=\"POST\" action=\"/update\" "
        "enctype=\"multipart/form-data\"></form>"
        "<form id=\"uc\" method=\"POST\" action=\"/update/check\"></form>"
        "<form id=\"upr\" method=\"POST\" action=\"/update/prerelease\"></form>");
  installForm(w, "ui", m.update.latest.tag);
  installForm(w, "uip", m.update.latestPre.tag);
  w.raw("<footer>CYD Sky Tracker · ");
  w.t(WebStr::Version);
  w.raw(" <span id=\"ver\">");
  w.esc(installed);
  w.raw("</span> · <a href=\"/screenshot.bmp\" target=\"_blank\">");
  w.t(WebStr::Screenshot);
  w.raw("</a></footer></div><script>var T={day:\"");
  w.t(WebStr::CreditsPerDay);
  w.raw("\",lim:\"");
  w.t(WebStr::DailyLimit);
  w.raw("\",over:\"");
  w.t(WebStr::OverLimit);
  w.raw("\",up:\"");
  w.t(WebStr::FirmwareUploading);
  w.raw("\",full:\"");
  w.t(WebStr::FirmwareLooksLikeFull);
  w.raw("\",fail:\"");
  w.t(WebStr::OtaAborted);
  w.raw("\",lday:\"");
  w.t(WebStr::LightDay);
  w.raw("\",lnt:\"");
  w.t(WebStr::LightNight);
  w.raw("\",loff:\"");
  w.t(WebStr::LightScreenOff);
  w.raw("\",lwk:\"");
  w.t(WebStr::LightWoken);
  w.raw("\",lns:\"");
  w.t(WebStr::LightNotSynced);
  w.raw("\",lpz:\"");
  w.t(WebStr::LightPollingPaused);
  w.raw("\"};");
  // app.js; after an update, restart.js reloads once the board is back;
  // while "check now" waits, check_pending.js (kUpdateManualCheckExpiryMs).
  w.raw(kWebAppJs);
  if (m.restarting) w.raw(kWebRestartJs);
  else if (m.updateCheckPending) w.raw(kWebCheckPendingJs);
  w.raw("</script></body></html>");
}
