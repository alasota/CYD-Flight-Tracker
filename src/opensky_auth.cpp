#include "opensky_auth.h"

uint32_t openSkyDailyLimit(const OpenSkyAuthState &s) {
  return s.mode == OpenSkyAuthState::Mode::Account ? kOpenSkyAccountDailyLimit
                                                   : kOpenSkyAnonymousDailyLimit;
}

uint32_t credentialsHash(const std::string &id, const std::string &secret) {
  uint32_t h = 2166136261u;
  auto mix = [&h](unsigned char c) {
    h ^= c;
    h *= 16777619u;
  };
  for (char c : id) mix(static_cast<unsigned char>(c));
  mix(0x1F);
  for (char c : secret) mix(static_cast<unsigned char>(c));
  return h;
}

OpenSkyAuthState onCredentials(const OpenSkyAuthState &s, uint32_t hash, bool haveCredentials) {
  OpenSkyAuthState out = s;
  if (!haveCredentials) {
    out = OpenSkyAuthState{};
    return out;
  }
  if (s.mode == OpenSkyAuthState::Mode::NoCredentials || s.credentialsHash != hash) {
    out = OpenSkyAuthState{};
    out.mode = OpenSkyAuthState::Mode::Account;
    out.credentialsHash = hash;
  }
  return out;
}

OpenSkyAuthState onTokenResult(const OpenSkyAuthState &s, int httpCode, bool gotToken) {
  OpenSkyAuthState out = s;
  if (s.mode == OpenSkyAuthState::Mode::NoCredentials) return out;
  if (httpCode == 200 && gotToken) {
    out.mode = OpenSkyAuthState::Mode::Account;
    out.lastHttpCode = 0;
  } else if (httpCode == 400 || httpCode == 401 || httpCode == 403) {
    out.mode = OpenSkyAuthState::Mode::LoginFailed;
    out.lastHttpCode = httpCode;
  }
  return out;
}
