#include "admin/core/settings_form.h"

#include <cstdlib>

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
