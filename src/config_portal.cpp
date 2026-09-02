#include "config_portal.h"

#include <cstdlib>

float bboxAreaSqDeg(float radius_deg) {
  float side = 2.0f * radius_deg;
  return side * side;
}

int openSkyCreditCost(float radius_deg) {
  float area = bboxAreaSqDeg(radius_deg);
  if (area <= 25.0f) return 1;
  if (area <= 100.0f) return 2;
  if (area <= 400.0f) return 3;
  return 4;
}

#include <ArduinoJson.h>

OpenSkyCredentials parseOpenSkyCredentialsJson(const std::string &json) {
  OpenSkyCredentials result;

  if (json.size() > kMaxCredentialsJsonBytes) {
    return result;  // refuse to even attempt parsing an unexpectedly huge body
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return result;
  }
  if (doc["clientId"].isNull() || doc["clientSecret"].isNull()) {
    return result;
  }

  result.client_id = doc["clientId"].as<std::string>();
  result.client_secret = doc["clientSecret"].as<std::string>();
  result.ok = !result.client_id.empty() && !result.client_secret.empty();
  return result;
}

bool isValidFloatString(const std::string &s) {
  if (s.empty()) return false;
  const char *start = s.c_str();
  char *end = nullptr;
  std::strtof(start, &end);
  if (end == start) return false;  // no digits consumed at all
  while (*end == ' ' || *end == '\t') ++end;
  return *end == '\0';  // nothing but (optional trailing whitespace) left over
}

bool isValidUnsignedIntString(const std::string &s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

#ifdef ARDUINO

#include <ESPmDNS.h>
#include <WebServer.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "config_store.h"

namespace {

WebServer server(80);
bool active = false;

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

// Sends `s` as one chunk of the in-progress chunked response, skipping the
// call entirely when `s` is empty — WebServer::sendContent() with a
// zero-length payload prematurely ends the chunked stream, which would
// silently truncate the page (e.g. whenever a credential field is blank).
void sendChunk(const char *s) {
  size_t len = std::strlen(s);
  if (len > 0) {
    server.sendContent(s, len);
  }
}
void sendChunk(const std::string &s) { sendChunk(s.c_str()); }

// Streams the config page directly to the client instead of building it up
// as one large Arduino String (see CLAUDE.md review notes 4.1) — static
// chunks are literals (no allocation at all), dynamic values are formatted
// into small stack buffers via snprintf.
void sendConfigPage(const Config &cfg, const char *message, bool isError = false) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  sendChunk("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
  sendChunk("<title>CYD Sky Tracker Setup</title>");
  sendChunk(
      "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:1.5em}"
      "label{display:block;margin-top:1em}"
      "input{width:100%;box-sizing:border-box;padding:.4em;font-size:1em}"
      "input[type=checkbox]{width:auto;margin-left:.5em}"
      "button{margin-top:1.5em;padding:.6em 1.2em;font-size:1em}"
      "h2{margin-top:2em;border-top:1px solid #444;padding-top:1em}"
      "#credit{color:#ffa500;font-weight:bold}</style>");
  sendChunk("</head><body>");
  sendChunk("<h1>CYD Sky Tracker</h1>");

  if (message != nullptr) {
    sendChunk(isError ? "<p style=\"color:#ff6f6f\">" : "<p style=\"color:#7fff7f\">");
    sendChunk(message);
    sendChunk("</p>");
  }

  sendChunk("<h2>Load OpenSky credentials from file</h2>"
            "<form method=\"POST\" action=\"/upload_credentials\" enctype=\"multipart/form-data\">"
            "<label>OpenSky client JSON (the file downloaded from your OpenSky account page)"
            "<input type=\"file\" name=\"creds_file\" accept=\"application/json,.json\" required>"
            "</label>"
            "<button type=\"submit\">Upload</button>"
            "</form>");

  sendChunk("<h2>Settings</h2><form method=\"POST\" action=\"/save\">");

  char buf[64];

  std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(cfg.home_lat));
  sendChunk("<label>Home latitude<input type=\"text\" name=\"lat\" value=\"");
  sendChunk(buf);
  sendChunk("\"></label>");

  std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(cfg.home_lon));
  sendChunk("<label>Home longitude<input type=\"text\" name=\"lon\" value=\"");
  sendChunk(buf);
  sendChunk("\"></label>");

  std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(cfg.radius_deg));
  sendChunk("<label>Scan radius, deg (bbox = 2x this on each side)"
            "<input type=\"text\" id=\"radius\" name=\"radius\" value=\"");
  sendChunk(buf);
  sendChunk("\" oninput=\"updateCredit()\"></label>");

  std::snprintf(buf, sizeof(buf), "%d", openSkyCreditCost(cfg.radius_deg));
  sendChunk("<p>OpenSky credit cost per poll: <span id=\"credit\">");
  sendChunk(buf);
  sendChunk("</span></p>");

  std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(cfg.poll_interval_s));
  sendChunk("<label>Poll interval, seconds<input type=\"number\" name=\"poll_interval\" min=\"5\" "
            "value=\"");
  sendChunk(buf);
  sendChunk("\"></label>");

  sendChunk("<label>Auto-cycle through screens"
            "<input type=\"checkbox\" name=\"auto_cycle\" value=\"1\"");
  sendChunk(cfg.auto_cycle_enabled ? " checked" : "");
  sendChunk("></label>");

  std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(cfg.auto_cycle_interval_s));
  sendChunk("<label>Auto-cycle interval, seconds"
            "<input type=\"number\" name=\"auto_cycle_interval\" min=\"3\" value=\"");
  sendChunk(buf);
  sendChunk("\"></label>");

  sendChunk("<label>OpenSky client ID<input type=\"text\" name=\"client_id\" value=\"");
  sendChunk(htmlEscape(cfg.opensky_client_id));
  sendChunk("\"></label>");

  sendChunk("<label>OpenSky client secret<input type=\"password\" name=\"client_secret\" value=\"");
  sendChunk(htmlEscape(cfg.opensky_client_secret));
  sendChunk("\"></label>");

  sendChunk("<button type=\"submit\">Save</button></form>");

  // Mirrors openSkyCreditCost() above so the credit readout updates as the
  // user types, without a round trip to the device for every keystroke.
  sendChunk("<script>"
            "function creditCost(radius){"
            "var side=2*radius,area=side*side;"
            "if(area<=25)return 1;if(area<=100)return 2;if(area<=400)return 3;return 4;"
            "}"
            "function updateCredit(){"
            "var r=parseFloat(document.getElementById('radius').value)||0;"
            "document.getElementById('credit').textContent=creditCost(r);"
            "}"
            "</script>");
  sendChunk("</body></html>");

  server.sendContent("");  // 0-length chunk closes the chunked response
}

void handleRoot() {
  Config cfg = loadConfig();
  sendConfigPage(cfg, nullptr);
}

void handleSave() {
  Config cfg = loadConfig();

  // Reject malformed numeric input instead of silently treating it as 0
  // (which sanitizeConfig would then just clamp to a "valid-looking"
  // value with no feedback to the user) — see CLAUDE.md review notes 1.6.
  // Rejected fields keep their previous value.
  std::string rejected;

  if (server.hasArg("lat")) {
    std::string v = server.arg("lat").c_str();
    if (isValidFloatString(v)) {
      cfg.home_lat = std::strtof(v.c_str(), nullptr);
    } else {
      rejected += "lat ";
    }
  }
  if (server.hasArg("lon")) {
    std::string v = server.arg("lon").c_str();
    if (isValidFloatString(v)) {
      cfg.home_lon = std::strtof(v.c_str(), nullptr);
    } else {
      rejected += "lon ";
    }
  }
  if (server.hasArg("radius")) {
    std::string v = server.arg("radius").c_str();
    if (isValidFloatString(v)) {
      cfg.radius_deg = std::strtof(v.c_str(), nullptr);
    } else {
      rejected += "radius ";
    }
  }
  if (server.hasArg("poll_interval")) {
    std::string v = server.arg("poll_interval").c_str();
    if (isValidUnsignedIntString(v)) {
      cfg.poll_interval_s = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else {
      rejected += "poll_interval ";
    }
  }
  // Checkbox: the browser sends this arg only when it's ticked. handleSave
  // always runs on a full settings-form submit, so a missing arg here
  // genuinely means "unchecked", not "field absent".
  cfg.auto_cycle_enabled = server.hasArg("auto_cycle");

  if (server.hasArg("auto_cycle_interval")) {
    std::string v = server.arg("auto_cycle_interval").c_str();
    if (isValidUnsignedIntString(v)) {
      cfg.auto_cycle_interval_s = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else {
      rejected += "auto_cycle_interval ";
    }
  }

  if (server.hasArg("client_id")) cfg.opensky_client_id = server.arg("client_id").c_str();
  if (server.hasArg("client_secret")) {
    cfg.opensky_client_secret = server.arg("client_secret").c_str();
  }

  // The user has now been through the settings form at least once — see
  // CLAUDE.md review notes 1.5. This is what main.cpp checks to tell
  // "not configured yet" apart from "home really is at (0,0)".
  cfg.home_configured = true;

  saveConfig(cfg);  // sanitizes internally (see config_store)

  Config saved = loadConfig();
  if (!rejected.empty()) {
    std::string msg = "Saved, but ignored invalid value(s) for: " + rejected + "(kept previous value).";
    sendConfigPage(saved, msg.c_str(), true);
  } else {
    sendConfigPage(saved, "Saved.");
  }
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

// State for the in-progress credentials-file upload, accumulated across the
// UPLOAD_FILE_WRITE chunks WebServer delivers before handleCredentialsUpload
// runs with the completed request.
std::string uploadBuffer;
bool uploadTooLarge = false;
constexpr size_t kMaxUploadBytes = 4096;  // credentials JSON is a few dozen bytes

void handleCredentialsUploadChunk() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadBuffer.clear();
    uploadTooLarge = false;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadBuffer.size() + upload.currentSize > kMaxUploadBytes) {
      uploadTooLarge = true;
    } else {
      uploadBuffer.append(reinterpret_cast<const char *>(upload.buf), upload.currentSize);
    }
  }
}

void handleCredentialsUpload() {
  Config cfg = loadConfig();

  if (uploadTooLarge) {
    sendConfigPage(cfg, "Upload failed: file too large.", true);
    return;
  }

  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(uploadBuffer);
  if (!creds.ok) {
    sendConfigPage(cfg, "Upload failed: file did not contain a valid clientId/clientSecret.", true);
    return;
  }

  cfg.opensky_client_id = creds.client_id;
  cfg.opensky_client_secret = creds.client_secret;
  saveConfig(cfg);  // sanitizes internally (see config_store)

  Config saved = loadConfig();
  sendConfigPage(saved, "OpenSky credentials loaded from file.");
}

}  // namespace

void configPortalBegin() {
  if (active) return;

  MDNS.begin("cyd-sky");  // -> cyd-sky.local

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/upload_credentials", HTTP_POST, handleCredentialsUpload, handleCredentialsUploadChunk);
  server.onNotFound(handleNotFound);
  server.begin();

  MDNS.addService("http", "tcp", 80);

  active = true;
}

void configPortalLoop() {
  if (!active) return;
  server.handleClient();
}

bool configPortalIsActive() { return active; }

#endif  // ARDUINO
