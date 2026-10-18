#include "config_page.h"

#include <cstdio>

#include "config_portal.h"  // openSkyCreditCost()

const char *settingsTabId(SettingsTab tab) {
  switch (tab) {
    case SettingsTab::OpenSky:
      return "opensky";
    case SettingsTab::General:
      return "general";
    case SettingsTab::Location:
    default:
      return "location";
  }
}

SettingsTab settingsTabFromArg(const std::string &arg) {
  if (arg == "opensky") return SettingsTab::OpenSky;
  if (arg == "general") return SettingsTab::General;
  return SettingsTab::Location;
}

bool hasOpenSkyCredentials(const Config &cfg) {
  return !cfg.opensky_client_id.empty() && !cfg.opensky_client_secret.empty();
}

uint32_t openSkyDailyCredits(float radius_deg, uint32_t poll_interval_s) {
  const uint32_t poll = poll_interval_s == 0 ? 1 : poll_interval_s;
  const uint32_t requests = (86400u + poll - 1) / poll;
  return requests * static_cast<uint32_t>(openSkyCreditCost(radius_deg));
}

uint32_t openSkyDailyCreditLimit(bool authenticated) { return authenticated ? 4000u : 400u; }

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
    ".tabs{display:flex;gap:4px;padding:4px;background:var(--field);"
    "border:1px solid var(--line);border-radius:12px;margin-bottom:20px}"
    ".tabs button{flex:1;padding:9px 4px;background:none;border:0;border-radius:9px;"
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
    ".switch{display:flex;justify-content:space-between;align-items:center;gap:12px;"
    "margin-bottom:18px;cursor:pointer}"
    ".switch input{width:20px;height:20px;margin:0;accent-color:var(--accent)}"
    ".msg{border-radius:10px;padding:11px 13px;margin-bottom:20px;font-size:.875rem;"
    "border:1px solid}"
    ".msg.ok{color:var(--ok);border-color:#2e4a2e;background:#1a231a}"
    ".msg.err{color:var(--err);border-color:#553030;background:#271b1b}"
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
    "$('tab').value=t;"
    "document.querySelectorAll('.lang a').forEach(function(a){a.href='/lang?l='+a.dataset.l+'&tab='+t})}"
    "document.querySelectorAll('.tabs button').forEach(function(b){b.onclick=function(){show(b.dataset.t)}});"
    "function cost(r){var a=4*r*r;return a<=25?1:a<=100?2:a<=400?3:4}"
    "function calc(){"
    "var c=cost(parseFloat($('radius').value)||0),p=Math.max(parseInt($('poll').value)||0,1),"
    "d=Math.ceil(86400/p)*c,l=($('cid').value&&$('cs').value)?4000:400;"
    "$('cost').textContent=c;"
    "$('daily').innerHTML='≈ <b>'+d+'</b> '+T.day+' · '+T.lim+' '+l+"
    "(d>l?'<div class=warn>'+T.over+'</div>':'')}"
    "['radius','poll','cid','cs'].forEach(function(i){$(i).addEventListener('input',calc)});"
    "function pw(){var i=$('cs');i.type=i.type=='password'?'text':'password'}"
    "show($('tab').value);calc();";

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

}  // namespace

void renderConfigPage(const ConfigPageModel &m, const HtmlSink &emit) {
  const Config &c = m.cfg;
  const Language lang = c.web_language;
  Writer w(emit, lang);

  // ---- head + header
  w.raw("<!DOCTYPE html><html lang=\"");
  w.raw(languageCode(lang));
  w.raw("\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  w.t(WebStr::PageTitle);
  w.raw("</title>");
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
    w.raw("</div>");
  }

  // ---- tab bar
  w.raw("<div class=\"tabs\" role=\"tablist\">");
  const SettingsTab tabs[kSettingsTabCount] = {SettingsTab::Location, SettingsTab::OpenSky,
                                               SettingsTab::General};
  const WebStr tabLabels[kSettingsTabCount] = {WebStr::TabLocation, WebStr::TabOpenSky,
                                               WebStr::TabGeneral};
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
  w.raw("</label><div class=\"pw\"><input id=\"cs\" type=\"password\" name=\"client_secret\" "
        "autocomplete=\"off\" value=\"");
  w.esc(c.opensky_client_secret);
  w.raw("\"><button type=\"button\" class=\"ghost\" onclick=\"pw()\">");
  w.t(WebStr::ShowSecret);
  w.raw("</button></div><div class=\"hint\">");
  w.t(WebStr::CredentialsHint);
  w.raw("</div></div></div>");

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
  const uint32_t daily = openSkyDailyCredits(c.radius_deg, c.poll_interval_s);
  const uint32_t limit = openSkyDailyCreditLimit(authed);
  w.raw("<div class=\"stat\" id=\"daily\">\xE2\x89\x88 <b>");
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
  w.raw("</div></div></section>");

  // ---- General
  w.sectionOpen(SettingsTab::General, m.tab);
  w.raw("<div class=\"card\"><h2>");
  w.t(WebStr::SectionLanguage);
  w.raw("</h2>");
  w.languageSelect("lang", "language", WebStr::DisplayLanguage, c.language);
  w.languageSelect("wlang", "web_language", WebStr::WebLanguage, c.web_language);
  w.raw("</div><div class=\"card\"><h2>");
  w.t(WebStr::SectionScreens);
  w.raw("</h2><label class=\"switch\"><span>");
  w.t(WebStr::AutoCycle);
  w.raw(c.auto_cycle_enabled
            ? "</span><input type=\"checkbox\" name=\"auto_cycle\" value=\"1\" checked></label>"
            : "</span><input type=\"checkbox\" name=\"auto_cycle\" value=\"1\"></label>");
  w.field("aci", WebStr::AutoCycleInterval,
          "type=\"number\" name=\"auto_cycle_interval\" min=\"3\"",
          fmtU(c.auto_cycle_interval_s));
  w.raw("</div><div class=\"card\"><h2>");
  w.t(WebStr::SectionPhases);
  w.raw("</h2>");
  const WebStr nearHint = WebStr::NearAirportHint;
  const WebStr climbHint = WebStr::ClimbRateHint;
  w.field("near", WebStr::NearAirport, "name=\"near_airport_km\" inputmode=\"decimal\"",
          fmt("%.1f", c.near_airport_km), &nearHint);
  w.field("climb", WebStr::ClimbRate, "name=\"climb_rate_mps\" inputmode=\"decimal\"",
          fmt("%.1f", c.climb_rate_threshold_mps), &climbHint);
  w.raw("</div></section>");

  // ---- save, upload form, footer, script
  w.raw("<button type=\"submit\" class=\"save\">");
  w.t(WebStr::Save);
  w.raw("</button></form>"
        "<form id=\"up\" method=\"POST\" action=\"/upload_credentials\" "
        "enctype=\"multipart/form-data\"></form>"
        "<footer><a href=\"/screenshot.bmp\" target=\"_blank\">");
  w.t(WebStr::Screenshot);
  w.raw("</a> · CYD Sky Tracker</footer></div><script>var T={day:\"");
  w.t(WebStr::CreditsPerDay);
  w.raw("\",lim:\"");
  w.t(WebStr::DailyLimit);
  w.raw("\",over:\"");
  w.t(WebStr::OverLimit);
  w.raw("\"};");
  w.raw(kScript);
  w.raw("</script></body></html>");
}
