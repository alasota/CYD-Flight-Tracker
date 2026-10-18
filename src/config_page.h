// config_page — the HTML of the config web page (served by config_portal),
// rendered as a stream of chunks so the ESP32 never builds the whole page in
// RAM. Four tabs (Location / OpenSky client / General / Backup), PL or EN text from
// web_i18n (Config::web_language), dark theme with the display's peach as
// the single accent colour.
//
// Pure — no WebServer / Arduino dependency: the caller supplies the sink
// (config_portal sends each chunk over HTTP, the native tests collect them
// into a string). Tested under `pio test -e native`.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "config_store.h"  // Config
#include "web_i18n.h"      // WebStr

enum class SettingsTab : uint8_t { Location, OpenSky, General, Backup };
constexpr int kSettingsTabCount = 4;

// "location" / "opensky" / "general" / "backup" — the ?tab= / hidden-field value.
const char *settingsTabId(SettingsTab tab);
// Inverse of settingsTabId(); anything unknown -> Location.
SettingsTab settingsTabFromArg(const std::string &arg);

// True when both OpenSky OAuth fields are set (else the firmware polls
// anonymously).
bool hasOpenSkyCredentials(const Config &cfg);

// Rough OpenSky credit use per day if the display polls non-stop:
// ceil(86400 / poll_interval_s) requests x openSkyCreditCost(radius).
// poll_interval_s 0 is treated as 1.
uint32_t openSkyDailyCredits(float radius_deg, uint32_t poll_interval_s);

// OpenSky's daily credit allowance: 4000 with an account, 400 anonymous
// (CLAUDE.md "Rate limits / credits").
uint32_t openSkyDailyCreditLimit(bool authenticated);

// &, <, >, " escaped — for user-entered values put into attributes.
std::string htmlEscape(const std::string &in);

struct ConfigPageModel {
  Config cfg;
  SettingsTab tab = SettingsTab::Location;  // tab shown on load
  bool hasMessage = false;
  WebStr message = WebStr::Saved;
  bool isError = false;
  std::string messageDetail;  // appended after the message (escaped on output)
};

using HtmlSink = std::function<void(const char *chunk)>;

// Emits the whole page, in order, as a series of chunks (never an empty one).
void renderConfigPage(const ConfigPageModel &model, const HtmlSink &emit);
