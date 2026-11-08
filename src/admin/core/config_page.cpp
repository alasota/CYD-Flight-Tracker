#include "admin/core/config_page.h"

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

// "<b>v1.3.0</b> · published 20.10.2026 · What is new" for one release.
void releaseLine(Writer &w, const ReleaseInfo &r) {
  w.raw("<b>");
  w.esc(r.tag);
  w.raw("</b>");
  const std::string date = formatIsoDate(r.publishedAt);
  if (!date.empty()) {
    w.raw(" \xC2\xB7 ");
    w.t(WebStr::UpdPublished);
    w.raw(" ");
    w.esc(date);
  }
  if (!r.htmlUrl.empty()) {
    w.raw(" \xC2\xB7 <a href=\"");
    w.esc(r.htmlUrl);
    w.raw("\" target=\"_blank\" rel=\"noopener\">");
    w.t(WebStr::UpdWhatsNew);
    w.raw("</a>");
  }
}

// Firmware tab, first card: what GitHub has, and the buttons to get it.
// The buttons belong to the #uc / #ui / #uip / #upr forms after the main one.
void updateCard(Writer &w, const ConfigPageModel &m, const std::string &installed) {
  const UpdateState &u = m.update;
  w.raw("<div class=\"card\" id=\"gh\"><h2>");
  w.t(WebStr::UpdCardTitle);
  w.raw("</h2>");
  if (u.lastInstall != OtaResult::NoFile && u.lastInstall != OtaResult::Ok) {
    w.raw("<div class=\"msg err\">");
    w.t(WebStr::UpdLastInstallFailed);
    w.raw(" (");
    w.esc(u.lastInstallTag);
    w.raw("): ");
    w.t(otaResultMessage(u.lastInstall));
    w.raw("</div>");
  }
  w.raw("<div class=\"stat\" style=\"margin:0 0 14px\">");
  w.t(WebStr::FirmwareInstalled);
  w.raw(": <b id=\"fwv\">");
  w.esc(installed);
  w.raw("</b>");
  if (!u.latest.tag.empty()) {
    w.raw("<br>");
    w.t(WebStr::UpdLatest);
    w.raw(": ");
    releaseLine(w, u.latest);
  }
  w.raw("</div>");

  const bool good = !m.updateCheckPending &&
                    (u.status == UpdateStatus::UpToDate || u.status == UpdateStatus::Available);
  w.raw(good ? "<span class=\"pill ok\" id=\"ups\"><span class=\"dot\"></span>"
             : "<span class=\"pill\" id=\"ups\"><span class=\"dot\"></span>");
  w.t(m.updateCheckPending ? WebStr::UpdChecking : updateStatusMessage(u.status));
  w.raw("</span>");
  if (u.checkedAt > 0) {
    w.raw("<div class=\"hint\" style=\"margin:-8px 0 14px\">");
    w.t(WebStr::UpdChecked);
    w.raw(" ");
    w.esc(formatDate(u.checkedAt) + " " + formatTime(u.checkedAt));
    w.raw("</div>");
  }

  if (statusAllowsInstall(u.status) && !u.latest.tag.empty() && u.latest.tag != installed) {
    w.raw("<button form=\"ui\" class=\"wide\" id=\"upi\" style=\"margin-bottom:10px\">");
    w.t(u.status == UpdateStatus::BackToRelease ? WebStr::UpdBackToRelease : WebStr::UpdInstall);
    w.raw(" ");
    w.esc(u.latest.tag);
    w.raw("</button>");
  }
  w.raw("<button form=\"uc\" class=\"ghost wide\">");
  w.t(WebStr::UpdCheckNow);
  w.raw("</button>");

  // Development versions: only on request (spec/02 §8.6).
  const bool showPre = m.cfg.update.show_prerelease;
  w.raw("<label class=\"switch\" style=\"margin:18px 0 0\"><span>");
  w.t(WebStr::UpdShowPrerelease);
  w.raw(showPre ? "</span><input type=\"checkbox\" name=\"on\" value=\"1\" form=\"upr\" "
                  "onchange=\"this.form.submit()\" checked></label>"
                : "</span><input type=\"checkbox\" name=\"on\" value=\"1\" form=\"upr\" "
                  "onchange=\"this.form.submit()\"></label>");
  if (showPre && preReleaseWorthShowing(u.latestPre, u.latest)) {
    w.raw("<div class=\"stat\" id=\"upre\" style=\"margin:14px 0 10px\">");
    w.t(WebStr::UpdLatestPre);
    w.raw(": ");
    releaseLine(w, u.latestPre);
    w.raw("<div class=\"warn\">");
    w.t(WebStr::UpdPreWarning);
    w.raw("</div></div>");
    if (u.latestPre.tag == installed) {
      w.raw("<div class=\"hint\">");
      w.esc(u.latestPre.tag);
      w.raw(" \xC2\xB7 ");
      w.t(WebStr::UpdInstalledHere);
      w.raw("</div>");
    } else {
      w.raw("<button form=\"uip\" class=\"ghost wide\" id=\"upip\">");
      w.t(WebStr::UpdInstall);
      w.raw(" ");
      w.esc(u.latestPre.tag);
      w.raw("</button>");
    }
  }
  w.raw("</div>");
}

// General tab, "Display" card: brightness, night mode, and what the light
// sensor / backlight do right now (spec/03 §4.4). The script hides the
// fields of the modes not chosen (#blauto / #blfix / #ntsch / #ntopt).
void displayCard(Writer &w, const ConfigPageModel &m, Language lang) {
  const Settings &c = m.cfg;
  const BacklightStatus &l = m.light;
  w.raw("<div class=\"card\" id=\"display\"><h2>");
  w.t(WebStr::SectionDisplay);
  w.raw("</h2>");
  const Writer::Option modes[] = {{"auto", WebStr::BrightnessAuto},
                                  {"fixed", WebStr::BrightnessFixed}};
  w.select("blm", "brightness_mode", WebStr::BrightnessMode, modes, 2,
           c.backlight.brightness_auto ? "auto" : "fixed");
  w.raw(c.backlight.brightness_auto ? "<div id=\"blauto\">" : "<div id=\"blauto\" hidden>");
  w.raw("<div class=\"row\">");
  w.field("blmin", WebStr::BrightnessMin, "type=\"number\" name=\"bl_min\" min=\"5\" max=\"100\"",
          fmtU(c.backlight.brightness_min_pct));
  w.field("blmax", WebStr::BrightnessMax, "type=\"number\" name=\"bl_max\" min=\"5\" max=\"100\"",
          fmtU(c.backlight.brightness_max_pct));
  w.raw("</div><div class=\"hint\" style=\"margin:0 0 18px\">");
  w.t(WebStr::BrightnessAutoHint);
  w.raw("</div></div>");
  w.raw(c.backlight.brightness_auto ? "<div id=\"blfix\" hidden>" : "<div id=\"blfix\">");
  w.field("blfx", WebStr::BrightnessFixedPct,
          "type=\"number\" name=\"bl_fixed\" min=\"5\" max=\"100\"",
          fmtU(c.backlight.brightness_fixed_pct));
  w.raw("</div>");

  const Writer::Option nights[] = {{"off", WebStr::NightOff},
                                   {"schedule", WebStr::NightSchedule},
                                   {"sun", WebStr::NightSun}};
  w.select("ntm", "night_mode", WebStr::NightModeLabel, nights, 3,
           nightModeCode(c.backlight.night_mode));
  w.raw(c.backlight.night_mode == NightMode::Schedule ? "<div id=\"ntsch\">"
                                                      : "<div id=\"ntsch\" hidden>");
  w.raw("<div class=\"row\" style=\"margin-bottom:18px\">");
  w.field("ntf", WebStr::NightFrom, "type=\"time\" name=\"night_from\"",
          formatClockMinutes(c.backlight.night_from_min));
  w.field("ntt", WebStr::NightTo, "type=\"time\" name=\"night_to\"",
          formatClockMinutes(c.backlight.night_to_min));
  w.raw("</div></div>");
  w.raw(c.backlight.night_mode == NightMode::Off ? "<div id=\"ntopt\" hidden>"
                                                 : "<div id=\"ntopt\">");
  const WebStr nightHint = WebStr::NightBrightnessHint;
  w.field("ntpct", WebStr::NightBrightness,
          "type=\"number\" name=\"night_pct\" min=\"0\" max=\"100\"", fmtU(c.backlight.night_pct),
          &nightHint);
  w.field("ntw", WebStr::NightWake, "type=\"number\" name=\"night_wake_s\" min=\"10\" max=\"600\"",
          fmtU(c.backlight.night_wake_s));
  w.raw("<label class=\"switch\" style=\"margin:0\"><span>");
  w.t(WebStr::NightPause);
  w.raw(c.backlight.night_pause_polling
            ? "</span><input type=\"checkbox\" id=\"ntp\" name=\"night_pause\" value=\"1\" checked>"
            : "</span><input type=\"checkbox\" id=\"ntp\" name=\"night_pause\" value=\"1\">");
  w.raw("</label><div class=\"hint\">");
  w.t(WebStr::NightPauseHint);
  const uint32_t poll = c.poll.interval_s == 0 ? 1 : c.poll.interval_s;
  w.raw(" \xE2\x89\x88 <b id=\"ntsv\">");
  w.uint((nightMinutesToday(c.backlight, l) * 60u + poll - 1) / poll);
  w.raw("</b> ");
  w.t(WebStr::NightPauseSaving);
  w.raw(".</div></div>");

  // What the board does right now (live: GET /light.json).
  w.raw("<div class=\"stat\" id=\"light\">");
  w.t(WebStr::LightNow);
  w.raw(": ");
  w.t(WebStr::LightSensor);
  w.raw(" <b id=\"lr\">");
  w.uint(l.raw);
  w.raw("</b> (");
  w.t(WebStr::LightAverage);
  w.raw(" <span id=\"la\">");
  w.uint(static_cast<uint32_t>(l.avg + 0.5f));
  w.raw("</span>) \xC2\xB7 ");
  w.t(WebStr::LightLevel);
  w.raw(" <b id=\"lp\">");
  w.uint(l.pct);
  w.raw("</b> % \xC2\xB7 <span id=\"ls\">");
  w.esc(lightStateText(l, lang));
  w.raw("</span><br>");
  if (!c.home.configured) {
    w.t(WebStr::SunNeedsLocation);
  } else if (!l.sun_known) {
    w.t(WebStr::SunNeedsClock);
  } else if (l.sun_local.kind == SunTimes::Kind::PolarDay) {
    w.t(WebStr::SunPolarDay);
  } else if (l.sun_local.kind == SunTimes::Kind::PolarNight) {
    w.t(WebStr::SunPolarNight);
  } else {
    w.t(WebStr::SunToday);
    w.raw(" ");
    w.raw(formatClockMinutes(l.sun_local.sunrise_min));
    w.raw(", ");
    w.t(WebStr::SunSet);
    w.raw(" ");
    w.raw(formatClockMinutes(l.sun_local.sunset_min));
    w.raw(" ");
    w.t(WebStr::SunForLocation);
  }
  w.raw("</div></div>");
}

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

  // ---- Location
  w.sectionOpen(SettingsTab::Location, m.tab);
  w.raw("<div class=\"card\"><p class=\"intro\">");
  w.t(WebStr::LocationIntro);
  w.raw("</p><div class=\"row\">");
  w.field("lat", WebStr::Latitude, "name=\"lat\" inputmode=\"decimal\" autocomplete=\"off\"",
          fmt("%.5f", c.home.lat));
  w.field("lon", WebStr::Longitude, "name=\"lon\" inputmode=\"decimal\" autocomplete=\"off\"",
          fmt("%.5f", c.home.lon));
  w.raw("</div><div class=\"hint\" style=\"margin-bottom:18px\">");
  w.t(WebStr::LatLonHint);
  w.raw("</div>");
  const WebStr radiusHint = WebStr::RadiusHint;
  w.field("radius", WebStr::Radius, "name=\"radius\" inputmode=\"decimal\" autocomplete=\"off\"",
          fmt("%.2f", c.home.radius_deg), &radiusHint);
  w.raw("<div class=\"stat\">");
  w.t(WebStr::CreditsPerRequest);
  w.raw(": <b id=\"cost\">");
  w.uint(static_cast<uint32_t>(openSkyCreditCost(c.home.radius_deg)));
  w.raw("</b></div></div></section>");

  // ---- OpenSky client
  const bool authed = hasOpenSkyCredentials(c);
  // The login state for the credentials in this config: other credentials
  // than the ones the board last tried (just saved) read as Account.
  const OpenSkyAuthState auth =
      onCredentials(m.auth, credentialsHash(c.opensky.client_id, c.opensky.client_secret), authed);
  const bool loginFailed = auth.mode == OpenSkyAuthState::Mode::LoginFailed;
  w.sectionOpen(SettingsTab::OpenSky, m.tab);
  if (loginFailed) {
    w.raw("<div class=\"card\"><span class=\"pill err\"><span class=\"dot\"></span>");
    w.t(WebStr::StatusLoginFailed);
  } else if (authed) {
    w.raw("<div class=\"card\"><span class=\"pill ok\"><span class=\"dot\"></span>");
    w.t(WebStr::StatusAccount);
  } else {
    w.raw("<div class=\"card\"><span class=\"pill\"><span class=\"dot\"></span>");
    w.t(WebStr::StatusAnonymous);
  }
  w.raw("</span>");
  if (loginFailed) {
    w.raw("<div class=\"stat quota warn\" id=\"loginwarn\">");
    w.esc(loginFailedDetailText(auth, m.nowMs, m.nowEpoch, lang));
    w.raw("</div>");
  }
  // The "without an account: 400" hint explains the trade-off; in LoginFailed
  // the warning above already says it (and an account *is* set up).
  if (!loginFailed) {
    w.raw("<p class=\"intro\">");
    w.t(WebStr::OpenSkyIntro);
    w.raw("</p>");
  }
  w.field("cid", WebStr::ClientId,
          "name=\"client_id\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\"",
          c.opensky.client_id);
  w.raw("<div class=\"f\"><label for=\"cs\">");
  w.t(WebStr::ClientSecret);
  // The saved secret never goes into the page (anyone could read it from the
  // source): the field stays empty, empty = keep it.
  w.raw("</label><div class=\"pw\"><input id=\"cs\" type=\"password\" name=\"client_secret\" "
        "autocomplete=\"off\" value=\"\"");
  if (!c.opensky.client_secret.empty()) {
    w.raw(" data-saved=\"1\" placeholder=\"");
    w.t(WebStr::SecretKeepPlaceholder);
    w.raw("\"");
  }
  w.raw("><button type=\"button\" class=\"ghost\" onclick=\"pw()\">");
  w.t(WebStr::ShowSecret);
  w.raw("</button></div><div class=\"hint\">");
  w.t(WebStr::CredentialsHint);
  w.raw("</div></div>");
  if (authed) {
    w.raw("<label class=\"switch\"><span>");
    w.t(WebStr::ClearCredentials);
    w.raw("</span><input type=\"checkbox\" name=\"clear_credentials\" value=\"1\"></label>");
  }
  w.raw("</div>");

  // Upload: its controls live here but belong to the separate #up form
  // (form="up") — forms can't nest.
  w.raw("<div class=\"card\"><h2>");
  w.t(WebStr::UploadTitle);
  w.raw("</h2><div class=\"pw\"><input type=\"file\" name=\"creds_file\" form=\"up\" "
        "accept=\"application/json,.json\" required><button form=\"up\" class=\"ghost\">");
  w.t(WebStr::UploadButton);
  w.raw("</button></div><div class=\"hint\">");
  w.t(WebStr::UploadHint);
  w.raw("</div></div>");

  w.raw("<div class=\"card\">");
  w.field("poll", WebStr::PollInterval, "type=\"number\" name=\"poll_interval\" min=\"5\"",
          fmtU(c.poll.interval_s));
  const uint32_t daily =
      openSkyDailyCredits(c.home.radius_deg, c.poll.interval_s,
                          pausedMinutesPerDay(c.backlight, m.light));
  const uint32_t limit = openSkyDailyLimit(auth);
  w.raw(loginFailed ? "<div class=\"stat\" id=\"daily\" data-lf=\"1\" data-night=\""
                    : "<div class=\"stat\" id=\"daily\" data-night=\"");
  w.uint(nightMinutesToday(c.backlight, m.light));
  w.raw("\">\xE2\x89\x88 <b>");
  w.uint(daily);
  w.raw("</b> ");
  w.t(WebStr::CreditsPerDay);
  w.raw(" \xC2\xB7 ");
  w.t(WebStr::DailyLimit);
  w.raw(" ");
  w.uint(limit);
  if (daily > limit) {
    w.raw("<div class=\"warn\">");
    w.t(WebStr::OverLimit);
    w.raw("</div>");
  }
  w.raw("</div>");
  // What OpenSky itself last said about the credits (not an estimate).
  bool exhausted = false;
  const std::string quotaText = quotaStatusText(m.quota, m.quotaResetInS, lang, &exhausted);
  w.raw(exhausted ? "<div class=\"stat quota warn\" id=\"quota\">"
                  : "<div class=\"stat quota\" id=\"quota\">");
  w.esc(quotaText);
  w.raw("</div></div></section>");

  // ---- General
  w.sectionOpen(SettingsTab::General, m.tab);
  w.raw("<div class=\"card\"><h2>");
  w.t(WebStr::SectionLanguage);
  w.raw("</h2>");
  w.languageSelect("lang", "language", WebStr::DisplayLanguage, c.display.language);
  w.languageSelect("wlang", "web_language", WebStr::WebLanguage, c.web.language);
  w.raw("</div>");
  displayCard(w, m, lang);
  w.raw("<div class=\"card\"><h2>");
  w.t(WebStr::SectionScreens);
  w.raw("</h2><label class=\"switch\"><span>");
  w.t(WebStr::AutoCycle);
  w.raw(c.auto_cycle.enabled
            ? "</span><input type=\"checkbox\" name=\"auto_cycle\" value=\"1\" checked></label>"
            : "</span><input type=\"checkbox\" name=\"auto_cycle\" value=\"1\"></label>");
  w.field("aci", WebStr::AutoCycleInterval,
          "type=\"number\" name=\"auto_cycle_interval\" min=\"3\"",
          fmtU(c.auto_cycle.interval_s));
  w.raw("<label class=\"switch\" style=\"margin:0\"><span>");
  w.t(WebStr::UpdAutoCheck);
  w.raw(c.update.check_enabled
            ? "</span><input type=\"checkbox\" name=\"update_check\" value=\"1\" checked></label>"
            : "</span><input type=\"checkbox\" name=\"update_check\" value=\"1\"></label>");
  w.raw("</div><div class=\"card\"><h2>");
  w.t(WebStr::SectionPhases);
  w.raw("</h2>");
  const WebStr nearHint = WebStr::NearAirportHint;
  const WebStr climbHint = WebStr::ClimbRateHint;
  w.field("near", WebStr::NearAirport, "name=\"near_airport_km\" inputmode=\"decimal\"",
          fmt("%.1f", c.phase.near_airport_km), &nearHint);
  w.field("climb", WebStr::ClimbRate, "name=\"climb_rate_mps\" inputmode=\"decimal\"",
          fmt("%.1f", c.phase.climb_rate_threshold_mps), &climbHint);
  w.raw("</div>");

  // Panel password: the fields are never pre-filled; both empty = no change.
  const bool hasPassword = !c.web.password.empty();
  w.raw("<div class=\"card\" id=\"access\"><h2>");
  w.t(WebStr::SectionAccess);
  w.raw("</h2>");
  if (hasPassword) {
    w.raw("<span class=\"pill ok\"><span class=\"dot\"></span>");
    w.t(WebStr::AccessPasswordSet);
    w.raw("</span>");
  } else {
    w.raw("<div class=\"nopw\">");
    w.t(WebStr::AccessNoPassword);
    w.raw("</div>");
  }
  w.raw("<div class=\"row\">");
  w.field("np", WebStr::NewPassword,
          "type=\"password\" name=\"new_password\" autocomplete=\"new-password\" maxlength=\"64\"",
          "");
  w.field("np2", WebStr::RepeatPassword,
          "type=\"password\" name=\"new_password2\" autocomplete=\"new-password\" maxlength=\"64\"",
          "");
  w.raw("</div><div class=\"hint\">");
  w.t(WebStr::PasswordHint);
  w.raw("</div>");
  if (hasPassword) {
    w.raw("<label class=\"switch\" style=\"margin:16px 0 0\"><span>");
    w.t(WebStr::ClearPassword);
    w.raw("</span><input type=\"checkbox\" name=\"clear_password\" value=\"1\"></label>");
  }
  w.raw("</div></section>");

  // ---- Backup: download / restore. Its controls belong to their own forms
  // (#bk, #rs, below the settings form — forms can't nest); the tab has no
  // use for the settings Save button, so it's hidden there.
  w.sectionOpen(SettingsTab::Backup, m.tab);
  w.raw("<div class=\"card\"><p class=\"intro\">");
  w.t(WebStr::BackupIntro);
  w.raw("</p><h2>");
  w.t(WebStr::BackupSaveTitle);
  w.raw("</h2><label class=\"switch\"><span>");
  w.t(WebStr::BackupIncludeSecrets);
  w.raw("</span><input type=\"checkbox\" name=\"secrets\" value=\"1\" form=\"bk\" checked>"
        "</label><div class=\"hint\" style=\"margin:-10px 0 16px\">");
  w.t(WebStr::BackupSecretsHint);
  w.raw("</div><button form=\"bk\" class=\"wide\">");
  w.t(WebStr::BackupDownload);
  w.raw("</button></div><div class=\"card\"><h2>");
  w.t(WebStr::RestoreTitle);
  w.raw("</h2><div class=\"pw\"><input type=\"file\" name=\"backup_file\" form=\"rs\" "
        "accept=\"application/json,.json\" required><button form=\"rs\" class=\"ghost\">");
  w.t(WebStr::RestoreButton);
  w.raw("</button></div><div class=\"hint\">");
  w.t(WebStr::RestoreHint);
  w.raw("</div></div></section>");

  // ---- Firmware: OTA update. Its controls belong to the #fw form.
  w.sectionOpen(SettingsTab::Firmware, m.tab);
  if (!hasPassword) {
    w.raw("<div class=\"nopw\">");
    w.t(WebStr::AccessNoPassword);
    w.raw("</div>");
  }
  updateCard(w, m, installed);
  w.raw("<div class=\"card\"><p class=\"intro\">");
  w.t(WebStr::FirmwareIntro);
  w.raw(" <a href=\"");
  w.raw(kReleasesUrl);
  w.raw("\" target=\"_blank\" rel=\"noopener\">");
  w.t(WebStr::FirmwareReleases);
  w.raw("</a></p><div class=\"f\"><label for=\"fwf\">");
  w.t(WebStr::FirmwareChoose);
  w.raw("</label><input id=\"fwf\" type=\"file\" name=\"firmware\" form=\"fw\" "
        "accept=\".bin,application/octet-stream\" required><div class=\"hint\">");
  w.t(WebStr::FirmwareHint);
  w.raw("</div></div><progress id=\"fwp\" max=\"100\" value=\"0\" hidden></progress>"
        "<div class=\"hint\" id=\"fws\" style=\"margin:0 0 12px\"></div>"
        "<button form=\"fw\" class=\"wide\" id=\"fwb\">");
  w.t(WebStr::FirmwareUpdateButton);
  w.raw("</button><div class=\"hint\">");
  w.t(WebStr::FirmwareDontPowerOff);
  w.raw("</div></div></section>");

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
