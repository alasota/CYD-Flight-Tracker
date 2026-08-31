#include "config_portal.h"

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

#ifdef ARDUINO

#include <ESPmDNS.h>
#include <WebServer.h>

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

String renderForm(const Config &cfg, const char *message, bool isError = false) {
  String html;
  html.reserve(2048);
  html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  html += "<title>CYD Sky Tracker Setup</title>";
  html +=
      "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:1.5em}"
      "label{display:block;margin-top:1em}"
      "input{width:100%;box-sizing:border-box;padding:.4em;font-size:1em}"
      "button{margin-top:1.5em;padding:.6em 1.2em;font-size:1em}"
      "h2{margin-top:2em;border-top:1px solid #444;padding-top:1em}"
      "#credit{color:#ffa500;font-weight:bold}</style>";
  html += "</head><body>";
  html += "<h1>CYD Sky Tracker</h1>";
  if (message != nullptr) {
    html += isError ? "<p style=\"color:#ff6f6f\">" : "<p style=\"color:#7fff7f\">";
    html += message;
    html += "</p>";
  }

  html += "<h2>Load OpenSky credentials from file</h2>";
  html += "<form method=\"POST\" action=\"/upload_credentials\" enctype=\"multipart/form-data\">";
  html += "<label>OpenSky client JSON (the file downloaded from your OpenSky account page)"
          "<input type=\"file\" name=\"creds_file\" accept=\"application/json,.json\" required>"
          "</label>";
  html += "<button type=\"submit\">Upload</button>";
  html += "</form>";

  html += "<h2>Settings</h2>";
  html += "<form method=\"POST\" action=\"/save\">";

  html += "<label>Home latitude<input type=\"text\" name=\"lat\" value=\"" +
          String(cfg.home_lat, 6) + "\"></label>";
  html += "<label>Home longitude<input type=\"text\" name=\"lon\" value=\"" +
          String(cfg.home_lon, 6) + "\"></label>";
  html += "<label>Scan radius, deg (bbox = 2x this on each side)"
          "<input type=\"text\" id=\"radius\" name=\"radius\" value=\"" +
          String(cfg.radius_deg, 2) + "\" oninput=\"updateCredit()\"></label>";
  html += "<p>OpenSky credit cost per poll: <span id=\"credit\">" +
          String(openSkyCreditCost(cfg.radius_deg)) + "</span></p>";
  html += "<label>Poll interval, seconds<input type=\"number\" name=\"poll_interval\" min=\"5\" "
          "value=\"" +
          String(cfg.poll_interval_s) + "\"></label>";
  html += "<label>OpenSky client ID<input type=\"text\" name=\"client_id\" value=\"" +
          String(htmlEscape(cfg.opensky_client_id).c_str()) + "\"></label>";
  html += "<label>OpenSky client secret<input type=\"password\" name=\"client_secret\" value=\"" +
          String(htmlEscape(cfg.opensky_client_secret).c_str()) + "\"></label>";
  html += "<button type=\"submit\">Save</button>";
  html += "</form>";

  // Mirrors openSkyCreditCost() above so the credit readout updates as the
  // user types, without a round trip to the device for every keystroke.
  html +=
      "<script>"
      "function creditCost(radius){"
      "var side=2*radius,area=side*side;"
      "if(area<=25)return 1;if(area<=100)return 2;if(area<=400)return 3;return 4;"
      "}"
      "function updateCredit(){"
      "var r=parseFloat(document.getElementById('radius').value)||0;"
      "document.getElementById('credit').textContent=creditCost(r);"
      "}"
      "</script>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  Config cfg = loadConfig();
  server.send(200, "text/html", renderForm(cfg, nullptr));
}

void handleSave() {
  Config cfg = loadConfig();

  if (server.hasArg("lat")) cfg.home_lat = server.arg("lat").toFloat();
  if (server.hasArg("lon")) cfg.home_lon = server.arg("lon").toFloat();
  if (server.hasArg("radius")) cfg.radius_deg = server.arg("radius").toFloat();
  if (server.hasArg("poll_interval")) {
    cfg.poll_interval_s = static_cast<uint32_t>(server.arg("poll_interval").toInt());
  }
  if (server.hasArg("client_id")) cfg.opensky_client_id = server.arg("client_id").c_str();
  if (server.hasArg("client_secret")) {
    cfg.opensky_client_secret = server.arg("client_secret").c_str();
  }

  saveConfig(cfg);  // sanitizes internally (see config_store)

  Config saved = loadConfig();
  server.send(200, "text/html", renderForm(saved, "Saved."));
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
    server.send(200, "text/html", renderForm(cfg, "Upload failed: file too large.", true));
    return;
  }

  OpenSkyCredentials creds = parseOpenSkyCredentialsJson(uploadBuffer);
  if (!creds.ok) {
    server.send(
        200, "text/html",
        renderForm(cfg, "Upload failed: file did not contain a valid clientId/clientSecret.",
                   true));
    return;
  }

  cfg.opensky_client_id = creds.client_id;
  cfg.opensky_client_secret = creds.client_secret;
  saveConfig(cfg);  // sanitizes internally (see config_store)

  Config saved = loadConfig();
  server.send(200, "text/html", renderForm(saved, "OpenSky credentials loaded from file."));
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
