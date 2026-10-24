#include "config_page.h"

#include <algorithm>
#include <cstdio>

#include "config_portal.h"  // openSkyCreditCost()
#include "time_sync.h"      // formatDate() / formatTime() — "checked" time
#include "version.h"        // firmwareVersion() — page footer

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

bool hasOpenSkyCredentials(const Config &cfg) {
  return !cfg.opensky_client_id.empty() && !cfg.opensky_client_secret.empty();
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

uint32_t nightMinutesToday(const Config &cfg, const BacklightStatus &light) {
  const NightWindow w = nightWindowFor(cfg.night_mode, cfg.night_from_min, cfg.night_to_min,
                                       light.sun_local, light.sun_known);
  return static_cast<uint32_t>(nightWindowMinutes(w));
}

uint32_t pausedMinutesPerDay(const Config &cfg, const BacklightStatus &light) {
  return cfg.night_pause_polling ? nightMinutesToday(cfg, light) : 0;
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

uint32_t openSkyDailyCreditLimit(bool authenticated) { return authenticated ? 4000u : 400u; }

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

// Same icon as the project page (site/favicon.svg), inlined as a data URI so
// the browser doesn't ask the device for /favicon.ico. '#' is %23 in a URI;
// single quotes keep it inside the double-quoted href.
const char kFavicon[] =
    "<link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,"
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
    "<rect width='32' height='32' rx='8' fill='%231d1f24'/>"
    "<path fill='%23ff9966' d='M5 18.5l2-1 5 1.5 4-7.5-2.5-6.5h2.5l6 6.5 5-.5c1.6 0 2.5.8 2.5 "
    "1.6s-.9 1.6-2.5 1.6l-5-.5-4.5 7h-2.5l1.5-6-5 .5-2 2.5h-2l1-3.5z'/></svg>\">";

// Colours follow the display (lcars_theme variant A): near-black
// background, muted greys, peach (#ff9966, THEME_CHROME_ACCENT) as the only
// accent — active tab, buttons, focus — with dark text on it, the way the
// device puts black text on its peach tab.
const char kStyle[] =
    "<style>"
    ":root{--accent:#ff9966;--accent-soft:#ffcc99;--on-accent:#17181c;--bg:#17181c;"
    "--card:#1d1f24;--field:#22242a;--line:#33353d;--text:#e7e7eb;--muted:#9a9ca6;"
    "--ok:#7bd37b;--err:#ff7a7a;color-scheme:dark}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--text);"
    "font:15px/1.5 system-ui,-apple-system,'Segoe UI',sans-serif}"
    ".wrap{max-width:440px;margin:0 auto;padding:32px 16px 48px}"
    "header{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;"
    "margin-bottom:28px}"
    "h1{font-size:1.5rem;font-weight:600;margin:0;letter-spacing:-.01em}"
    ".sub{color:var(--muted);font-size:.875rem;margin-top:2px}"
    ".lang{display:flex;border:1px solid var(--line);border-radius:999px;overflow:hidden;"
    "flex-shrink:0;margin-top:4px}"
    ".lang a{padding:4px 11px;font-size:.78rem;font-weight:600;color:var(--muted);"
    "text-decoration:none}"
    ".lang a.on{background:var(--accent);color:var(--on-accent)}"
    ".tabs{display:flex;flex-wrap:wrap;gap:4px;padding:4px;background:var(--field);"
    "border:1px solid var(--line);border-radius:12px;margin-bottom:20px}"
    ".tabs button{flex:1 1 auto;white-space:nowrap;padding:9px 8px;background:none;border:0;border-radius:9px;"
    "color:var(--muted);font:inherit;font-size:.85rem;font-weight:500;cursor:pointer}"
    ".tabs button.on{background:var(--accent);color:var(--on-accent);font-weight:600}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:14px;"
    "padding:20px;margin-bottom:16px}"
    "h2{font-size:.72rem;font-weight:600;letter-spacing:.08em;text-transform:uppercase;"
    "color:var(--muted);margin:0 0 14px}"
    ".intro{color:var(--muted);font-size:.875rem;margin:0 0 18px}"
    "label{display:block;font-size:.875rem;margin-bottom:6px}"
    ".f{margin-bottom:18px}.f:last-child{margin-bottom:0}"
    "input,select{width:100%;background:var(--field);color:var(--text);"
    "border:1px solid var(--line);border-radius:10px;padding:11px;font:inherit}"
    "input:focus,select:focus{outline:none;border-color:var(--accent)}"
    "input[type=file]{padding:8px;font-size:.85rem}"
    "input[type=file]::file-selector-button{background:var(--line);color:var(--text);"
    "border:0;border-radius:7px;padding:6px 10px;margin-right:10px;font:inherit}"
    ".hint{color:var(--muted);font-size:.8rem;margin-top:6px}"
    ".row{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    ".row .f{margin-bottom:0}"
    ".stat{padding:11px 13px;border:1px dashed var(--line);border-radius:10px;"
    "font-size:.85rem;color:var(--muted);margin-top:18px}"
    ".stat b{color:var(--accent);font-weight:600}"
    ".stat .warn{color:var(--err);margin-top:4px}"
    ".stat.quota{margin-top:10px;border-style:solid}"
    ".stat.quota.warn{color:var(--err);border-color:#553030}"
    ".pill{display:inline-flex;align-items:center;gap:8px;font-size:.8rem;padding:4px 11px;"
    "border-radius:999px;border:1px solid var(--line);color:var(--muted);margin-bottom:14px}"
    ".dot{width:8px;height:8px;border-radius:50%;background:var(--muted)}"
    ".pill.ok{color:var(--text)}.pill.ok .dot{background:var(--ok)}"
    ".pw{display:flex;gap:8px}.pw input{flex:1;min-width:0}"
    "button{background:var(--accent);border:0;border-radius:10px;color:var(--on-accent);"
    "font:inherit;font-weight:600;padding:11px 16px;cursor:pointer}"
    "button.ghost{background:none;border:1px solid var(--line);color:var(--text);"
    "font-weight:500;white-space:nowrap}"
    ".save{width:100%;padding:13px;margin-top:4px}"
    "button.wide{width:100%}"
    ".switch{display:flex;justify-content:space-between;align-items:center;gap:12px;"
    "margin-bottom:18px;cursor:pointer}"
    ".switch input{width:20px;height:20px;margin:0;accent-color:var(--accent)}"
    ".msg{border-radius:10px;padding:11px 13px;margin-bottom:20px;font-size:.875rem;"
    "border:1px solid}"
    ".msg.ok{color:var(--ok);border-color:#2e4a2e;background:#1a231a}"
    ".msg.err{color:var(--err);border-color:#553030;background:#271b1b}"
    "progress{width:100%;height:10px;margin:4px 0 6px;accent-color:var(--accent)}"
    "button:disabled{opacity:.5;cursor:default}"
    ".alert{color:var(--err);border:1px solid #553030;background:#271b1b;border-radius:10px;"
    "padding:11px 13px;margin-bottom:16px;font-size:.875rem}"
    "footer{text-align:center;color:var(--muted);font-size:.8rem;margin-top:32px}"
    "a{color:var(--accent-soft)}"
    "[hidden]{display:none!important}"
    "</style>";

// Tab switching, the language-toggle links following the current tab, and
// the live credit readouts (mirroring openSkyCreditCost() /
// openSkyDailyCredits() / openSkyDailyCreditLimit() above).
const char kScript[] =
    "function $(i){return document.getElementById(i)}"
    "function show(t){"
    "document.querySelectorAll('section[data-tab]').forEach(function(s){s.hidden=s.dataset.tab!=t});"
    "document.querySelectorAll('.tabs button').forEach(function(b){b.classList.toggle('on',b.dataset.t==t)});"
    "$('tab').value=t;$('save').hidden=t=='backup'||t=='firmware';"
    "document.querySelectorAll('.lang a').forEach(function(a){a.href='/lang?l='+a.dataset.l+'&tab='+t})}"
    "document.querySelectorAll('.tabs button').forEach(function(b){b.onclick=function(){show(b.dataset.t)}});"
    "function cost(r){var a=4*r*r;return a<=25?1:a<=100?2:a<=400?3:4}"
    "function calc(){"
    "var c=cost(parseFloat($('radius').value)||0),p=Math.max(parseInt($('poll').value)||0,1),"
    "n=parseInt($('daily').dataset.night)||0,"
    "d=Math.ceil((86400-($('ntp').checked&&$('ntm').value!='off'?n*60:0))/p)*c,"
    "l=($('cid').value&&($('cs').value||$('cs').dataset.saved))?4000:400;"
    "$('cost').textContent=c;$('ntsv').textContent=Math.ceil(n*60/p);"
    "$('daily').innerHTML='≈ <b>'+d+'</b> '+T.day+' · '+T.lim+' '+l+"
    "(d>l?'<div class=warn>'+T.over+'</div>':'')}"
    "['radius','poll','cid','cs'].forEach(function(i){$(i).addEventListener('input',calc)});"
    // Screen card: show only the fields of the chosen modes; the "Now" line
    // follows the board (GET /light.json) while the General tab is open.
    "function disp(){var a=$('blm').value=='auto',n=$('ntm').value;"
    "$('blauto').hidden=!a;$('blfix').hidden=a;$('ntsch').hidden=n!='schedule';$('ntopt').hidden=n=='off'}"
    "$('blm').onchange=disp;$('ntm').onchange=function(){disp();calc()};$('ntp').onchange=calc;disp();"
    "setInterval(function(){if($('tab').value!='general')return;"
    "fetch('/light.json',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){"
    "$('lr').textContent=j.raw;$('la').textContent=j.avg;$('lp').textContent=j.pct;"
    "$('ls').textContent=(!j.synced?T.lns:j.screen_off?T.loff:j.woken?T.lwk:j.night?T.lnt:T.lday)+"
    "(j.polling?'':' · '+T.lpz)}).catch(function(){})},2000);"
    "function pw(){var i=$('cs');i.type=i.type=='password'?'text':'password'}"
    // Firmware upload with a progress bar (a plain form post works too, just
    // without one). The answer page replaces this one.
    "$('fw').onsubmit=function(e){var i=$('fwf').files[0];if(!i)return;e.preventDefault();"
    "if(/-full/i.test(i.name)){$('fws').textContent=T.full;return}"
    "var x=new XMLHttpRequest(),d=new FormData();d.append('firmware',i);"
    "x.open('POST','/update?size='+i.size);"
    "x.upload.onprogress=function(v){if(!v.lengthComputable)return;var p=Math.round(v.loaded*100/v.total);"
    "$('fwp').hidden=false;$('fwp').value=p;$('fws').textContent=T.up+' '+p+'%'};"
    "x.onload=function(){document.open();document.write(x.responseText);document.close()};"
    "x.onerror=function(){$('fwb').disabled=false;$('fws').textContent=T.fail};"
    "$('fwb').disabled=true;$('fws').textContent=T.up+'…';x.send(d)};"
    "show($('tab').value);calc();";

// After a successful update: wait for the board to come back, then reload.
const char kRestartScript[] =
    "setTimeout(function p(){fetch('/',{cache:'no-store'}).then(function(r){"
    "if(r.ok||r.status==401)location.href='/?tab=firmware';else setTimeout(p,2000)})"
    ".catch(function(){setTimeout(p,2000)})},5000);";

class Writer {
 public:
  Writer(const HtmlSink &emit, Language lang) : emit_(emit), lang_(lang) {}
  void raw(const char *s) {
    if (s != nullptr && *s != '\0') emit_(s);
  }
  void raw(const std::string &s) { raw(s.c_str()); }
  void esc(const std::string &s) { raw(htmlEscape(s)); }
  void t(WebStr id) { raw(webTr(id, lang_)); }
  void uint(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
    raw(buf);
  }

  // <div class=f><label for=ID>LABEL</label><input ... value="VALUE"></div>
  // `attrs` = everything after "<input " up to the value.
  void field(const char *id, WebStr label, const char *attrs, const std::string &value,
             const WebStr *hint = nullptr) {
    raw("<div class=\"f\"><label for=\"");
    raw(id);
    raw("\">");
    t(label);
    raw("</label><input id=\"");
    raw(id);
    raw("\" ");
    raw(attrs);
    raw(" value=\"");
    esc(value);
    raw("\">");
    if (hint != nullptr) {
      raw("<div class=\"hint\">");
      t(*hint);
      raw("</div>");
    }
    raw("</div>");
  }

  void languageSelect(const char *id, const char *name, WebStr label, Language selected) {
    raw("<div class=\"f\"><label for=\"");
    raw(id);
    raw("\">");
    t(label);
    raw("</label><select id=\"");
    raw(id);
    raw("\" name=\"");
    raw(name);
    raw("\">");
    raw(selected == Language::PL ? "<option value=\"pl\" selected>Polski</option>"
                                 : "<option value=\"pl\">Polski</option>");
    raw(selected == Language::EN ? "<option value=\"en\" selected>English</option>"
                                 : "<option value=\"en\">English</option>");
    raw("</select></div>");
  }

  struct Option {
    const char *value;
    WebStr label;
  };
  // <div class=f><label>LABEL</label><select id name>...options...</select></div>
  void select(const char *id, const char *name, WebStr label, const Option *options, int count,
              const char *selected) {
    raw("<div class=\"f\"><label for=\"");
    raw(id);
    raw("\">");
    t(label);
    raw("</label><select id=\"");
    raw(id);
    raw("\" name=\"");
    raw(name);
    raw("\">");
    for (int i = 0; i < count; ++i) {
      raw("<option value=\"");
      raw(options[i].value);
      raw(std::string(options[i].value) == selected ? "\" selected>" : "\">");
      t(options[i].label);
      raw("</option>");
    }
    raw("</select></div>");
  }

  void sectionOpen(SettingsTab tab, SettingsTab active) {
    raw("<section data-tab=\"");
    raw(settingsTabId(tab));
    raw(tab == active ? "\">" : "\" hidden>");
  }

 private:
  const HtmlSink &emit_;
  Language lang_;
};

std::string fmt(const char *f, double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), f, v);
  return buf;
}
std::string fmtU(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
  return buf;
}

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

  const bool good = u.status == UpdateStatus::UpToDate || u.status == UpdateStatus::Available;
  w.raw(good ? "<span class=\"pill ok\" id=\"ups\"><span class=\"dot\"></span>"
             : "<span class=\"pill\" id=\"ups\"><span class=\"dot\"></span>");
  w.t(updateStatusMessage(u.status));
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
  const bool showPre = m.cfg.update_show_prerelease;
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
  const Config &c = m.cfg;
  const BacklightStatus &l = m.light;
  w.raw("<div class=\"card\" id=\"display\"><h2>");
  w.t(WebStr::SectionDisplay);
  w.raw("</h2>");
  const Writer::Option modes[] = {{"auto", WebStr::BrightnessAuto},
                                  {"fixed", WebStr::BrightnessFixed}};
  w.select("blm", "brightness_mode", WebStr::BrightnessMode, modes, 2,
           c.brightness_auto ? "auto" : "fixed");
  w.raw(c.brightness_auto ? "<div id=\"blauto\">" : "<div id=\"blauto\" hidden>");
  w.raw("<div class=\"row\">");
  w.field("blmin", WebStr::BrightnessMin, "type=\"number\" name=\"bl_min\" min=\"5\" max=\"100\"",
          fmtU(c.brightness_min_pct));
  w.field("blmax", WebStr::BrightnessMax, "type=\"number\" name=\"bl_max\" min=\"5\" max=\"100\"",
          fmtU(c.brightness_max_pct));
  w.raw("</div><div class=\"hint\" style=\"margin:0 0 18px\">");
  w.t(WebStr::BrightnessAutoHint);
  w.raw("</div></div>");
  w.raw(c.brightness_auto ? "<div id=\"blfix\" hidden>" : "<div id=\"blfix\">");
  w.field("blfx", WebStr::BrightnessFixedPct,
          "type=\"number\" name=\"bl_fixed\" min=\"5\" max=\"100\"", fmtU(c.brightness_fixed_pct));
  w.raw("</div>");

  const Writer::Option nights[] = {{"off", WebStr::NightOff},
                                   {"schedule", WebStr::NightSchedule},
                                   {"sun", WebStr::NightSun}};
  w.select("ntm", "night_mode", WebStr::NightModeLabel, nights, 3, nightModeCode(c.night_mode));
  w.raw(c.night_mode == NightMode::Schedule ? "<div id=\"ntsch\">" : "<div id=\"ntsch\" hidden>");
  w.raw("<div class=\"row\" style=\"margin-bottom:18px\">");
  w.field("ntf", WebStr::NightFrom, "type=\"time\" name=\"night_from\"",
          formatClockMinutes(c.night_from_min));
  w.field("ntt", WebStr::NightTo, "type=\"time\" name=\"night_to\"",
          formatClockMinutes(c.night_to_min));
  w.raw("</div></div>");
  w.raw(c.night_mode == NightMode::Off ? "<div id=\"ntopt\" hidden>" : "<div id=\"ntopt\">");
  const WebStr nightHint = WebStr::NightBrightnessHint;
  w.field("ntpct", WebStr::NightBrightness,
          "type=\"number\" name=\"night_pct\" min=\"0\" max=\"100\"", fmtU(c.night_pct),
          &nightHint);
  w.field("ntw", WebStr::NightWake, "type=\"number\" name=\"night_wake_s\" min=\"10\" max=\"600\"",
          fmtU(c.night_wake_s));
  w.raw("<label class=\"switch\" style=\"margin:0\"><span>");
  w.t(WebStr::NightPause);
  w.raw(c.night_pause_polling
            ? "</span><input type=\"checkbox\" id=\"ntp\" name=\"night_pause\" value=\"1\" checked>"
            : "</span><input type=\"checkbox\" id=\"ntp\" name=\"night_pause\" value=\"1\">");
  w.raw("</label><div class=\"hint\">");
  w.t(WebStr::NightPauseHint);
  const uint32_t poll = c.poll_interval_s == 0 ? 1 : c.poll_interval_s;
  w.raw(" \xE2\x89\x88 <b id=\"ntsv\">");
  w.uint((nightMinutesToday(c, l) * 60u + poll - 1) / poll);
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
  if (!c.home_configured) {
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
  const Config &c = m.cfg;
  const Language lang = c.web_language;
  Writer w(emit, lang);
  const std::string installed = m.installedVersion.empty() ? firmwareVersion() : m.installedVersion;

  // ---- head + header
  w.raw("<!DOCTYPE html><html lang=\"");
  w.raw(languageCode(lang));
  w.raw("\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  w.t(WebStr::PageTitle);
  w.raw("</title>");
  w.raw(kFavicon);
  w.raw(kStyle);
  w.raw("</head><body><div class=\"wrap\"><header><div><h1>Sky Tracker</h1><div class=\"sub\">");
  w.t(WebStr::Subtitle);
  w.raw(" · cyd-sky.local</div></div><nav class=\"lang\">");
  w.raw(lang == Language::PL ? "<a data-l=\"pl\" class=\"on\" href=\"/lang?l=pl\">PL</a>"
                             : "<a data-l=\"pl\" href=\"/lang?l=pl\">PL</a>");
  w.raw(lang == Language::EN ? "<a data-l=\"en\" class=\"on\" href=\"/lang?l=en\">EN</a>"
                             : "<a data-l=\"en\" href=\"/lang?l=en\">EN</a>");
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

  // ---- tab bar
  w.raw("<div class=\"tabs\" role=\"tablist\">");
  const SettingsTab tabs[kSettingsTabCount] = {SettingsTab::Location, SettingsTab::OpenSky,
                                               SettingsTab::General, SettingsTab::Backup,
                                               SettingsTab::Firmware};
  const WebStr tabLabels[kSettingsTabCount] = {WebStr::TabLocation, WebStr::TabOpenSky,
                                               WebStr::TabGeneral, WebStr::TabBackup,
                                               WebStr::TabFirmware};
  for (int i = 0; i < kSettingsTabCount; ++i) {
    w.raw("<button type=\"button\" data-t=\"");
    w.raw(settingsTabId(tabs[i]));
    w.raw(tabs[i] == m.tab ? "\" class=\"on\">" : "\">");
    w.t(tabLabels[i]);
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
          fmt("%.5f", c.home_lat));
  w.field("lon", WebStr::Longitude, "name=\"lon\" inputmode=\"decimal\" autocomplete=\"off\"",
          fmt("%.5f", c.home_lon));
  w.raw("</div><div class=\"hint\" style=\"margin-bottom:18px\">");
  w.t(WebStr::LatLonHint);
  w.raw("</div>");
  const WebStr radiusHint = WebStr::RadiusHint;
  w.field("radius", WebStr::Radius, "name=\"radius\" inputmode=\"decimal\" autocomplete=\"off\"",
          fmt("%.2f", c.radius_deg), &radiusHint);
  w.raw("<div class=\"stat\">");
  w.t(WebStr::CreditsPerRequest);
  w.raw(": <b id=\"cost\">");
  w.uint(static_cast<uint32_t>(openSkyCreditCost(c.radius_deg)));
  w.raw("</b></div></div></section>");

  // ---- OpenSky client
  const bool authed = hasOpenSkyCredentials(c);
  w.sectionOpen(SettingsTab::OpenSky, m.tab);
  w.raw(authed ? "<div class=\"card\"><span class=\"pill ok\"><span class=\"dot\"></span>"
               : "<div class=\"card\"><span class=\"pill\"><span class=\"dot\"></span>");
  w.t(authed ? WebStr::StatusAccount : WebStr::StatusAnonymous);
  w.raw("</span><p class=\"intro\">");
  w.t(WebStr::OpenSkyIntro);
  w.raw("</p>");
  w.field("cid", WebStr::ClientId,
          "name=\"client_id\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\"",
          c.opensky_client_id);
  w.raw("<div class=\"f\"><label for=\"cs\">");
  w.t(WebStr::ClientSecret);
  // The saved secret never goes into the page (anyone could read it from the
  // source): the field stays empty, empty = keep it.
  w.raw("</label><div class=\"pw\"><input id=\"cs\" type=\"password\" name=\"client_secret\" "
        "autocomplete=\"off\" value=\"\"");
  if (!c.opensky_client_secret.empty()) {
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
          fmtU(c.poll_interval_s));
  const uint32_t daily =
      openSkyDailyCredits(c.radius_deg, c.poll_interval_s, pausedMinutesPerDay(c, m.light));
  const uint32_t limit = openSkyDailyCreditLimit(authed);
  w.raw("<div class=\"stat\" id=\"daily\" data-night=\"");
  w.uint(nightMinutesToday(c, m.light));
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
  w.languageSelect("lang", "language", WebStr::DisplayLanguage, c.language);
  w.languageSelect("wlang", "web_language", WebStr::WebLanguage, c.web_language);
  w.raw("</div>");
  displayCard(w, m, lang);
  w.raw("<div class=\"card\"><h2>");
  w.t(WebStr::SectionScreens);
  w.raw("</h2><label class=\"switch\"><span>");
  w.t(WebStr::AutoCycle);
  w.raw(c.auto_cycle_enabled
            ? "</span><input type=\"checkbox\" name=\"auto_cycle\" value=\"1\" checked></label>"
            : "</span><input type=\"checkbox\" name=\"auto_cycle\" value=\"1\"></label>");
  w.field("aci", WebStr::AutoCycleInterval,
          "type=\"number\" name=\"auto_cycle_interval\" min=\"3\"",
          fmtU(c.auto_cycle_interval_s));
  w.raw("<label class=\"switch\" style=\"margin:0\"><span>");
  w.t(WebStr::UpdAutoCheck);
  w.raw(c.update_check_enabled
            ? "</span><input type=\"checkbox\" name=\"update_check\" value=\"1\" checked></label>"
            : "</span><input type=\"checkbox\" name=\"update_check\" value=\"1\"></label>");
  w.raw("</div><div class=\"card\"><h2>");
  w.t(WebStr::SectionPhases);
  w.raw("</h2>");
  const WebStr nearHint = WebStr::NearAirportHint;
  const WebStr climbHint = WebStr::ClimbRateHint;
  w.field("near", WebStr::NearAirport, "name=\"near_airport_km\" inputmode=\"decimal\"",
          fmt("%.1f", c.near_airport_km), &nearHint);
  w.field("climb", WebStr::ClimbRate, "name=\"climb_rate_mps\" inputmode=\"decimal\"",
          fmt("%.1f", c.climb_rate_threshold_mps), &climbHint);
  w.raw("</div>");

  // Panel password: the fields are never pre-filled; both empty = no change.
  const bool hasPassword = !c.web_password.empty();
  w.raw("<div class=\"card\" id=\"access\"><h2>");
  w.t(WebStr::SectionAccess);
  w.raw("</h2>");
  if (hasPassword) {
    w.raw("<span class=\"pill ok\"><span class=\"dot\"></span>");
    w.t(WebStr::AccessPasswordSet);
    w.raw("</span>");
  } else {
    w.raw("<div class=\"alert\">");
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
    w.raw("<div class=\"alert\">");
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
  w.raw(kScript);
  if (m.restarting) w.raw(kRestartScript);
  w.raw("</script></body></html>");
}
