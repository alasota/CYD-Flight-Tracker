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

TokenOutcome classifyTokenResponse(int httpCode, bool hasAccessToken) {
  if (httpCode == 200 && hasAccessToken) return TokenOutcome::Ok;
  if (httpCode == 400 || httpCode == 401 || httpCode == 403) return TokenOutcome::Rejected;
  return TokenOutcome::Transient;
}

OpenSkyAuthState onTokenResult(const OpenSkyAuthState &s, TokenOutcome o, int httpCode,
                               uint32_t nowMs) {
  OpenSkyAuthState out = s;
  if (s.mode == OpenSkyAuthState::Mode::NoCredentials) return out;
  switch (o) {
    case TokenOutcome::Ok:
      out.mode = OpenSkyAuthState::Mode::Account;
      out.lastHttpCode = 0;
      out.failedSinceMs = 0;
      out.nextTokenTryMs = 0;
      break;
    case TokenOutcome::Rejected:
      // The first rejection of a series sets "since when"; later ones keep it.
      if (s.mode != OpenSkyAuthState::Mode::LoginFailed) out.failedSinceMs = nowMs;
      out.mode = OpenSkyAuthState::Mode::LoginFailed;
      out.lastHttpCode = httpCode;
      out.nextTokenTryMs = nowMs + kLoginRetryMs;  // wraps like millis() does
      break;
    case TokenOutcome::Transient:
      // A working account just tries again on the next poll; a rejected one
      // keeps its code (not the 0 of "no answer") and waits another 30 min.
      if (s.mode == OpenSkyAuthState::Mode::LoginFailed) out.nextTokenTryMs = nowMs + kLoginRetryMs;
      break;
  }
  return out;
}

bool shouldTryToken(const OpenSkyAuthState &s, uint32_t nowMs) {
  switch (s.mode) {
    case OpenSkyAuthState::Mode::NoCredentials:
      return false;
    case OpenSkyAuthState::Mode::Account:
      return true;
    case OpenSkyAuthState::Mode::LoginFailed:
      // Signed difference: still right when millis() wrapped in between.
      return static_cast<int32_t>(nowMs - s.nextTokenTryMs) >= 0;
  }
  return false;
}

bool planStatesRequest(const OpenSkyAuthState &s, bool tokenValid) {
  return s.mode == OpenSkyAuthState::Mode::Account && tokenValid;
}

StatesDecision afterStatesResponse(const OpenSkyAuthState &s, int httpCode, bool usedBearer,
                                   bool tokenRefreshedThisPoll, uint32_t nowMs) {
  if (httpCode != 401 && httpCode != 403) return StatesDecision{StatesStep::Accept, s};
  // Anonymous and still rejected: nothing left to try.
  if (!usedBearer) return StatesDecision{StatesStep::AuthError, s};
  // A token can expire early on the server side: refresh once and retry.
  if (!tokenRefreshedThisPoll) return StatesDecision{StatesStep::RefreshTokenAndRetry, s};
  // Even a fresh token is rejected: the account doesn't work, go anonymous.
  return StatesDecision{StatesStep::RetryAnonymous,
                        onTokenResult(s, TokenOutcome::Rejected, httpCode, nowMs)};
}

std::string authChangeLogLine(const OpenSkyAuthState &prev, const OpenSkyAuthState &next) {
  if (prev.mode == next.mode) return {};
  if (next.mode == OpenSkyAuthState::Mode::LoginFailed) {
    return "login failed (http=" + std::to_string(next.lastHttpCode) +
           ") — check client_id/secret; using anonymous access, next try in " +
           std::to_string(kLoginRetryMs / 60000u) + " min";
  }
  if (prev.mode == OpenSkyAuthState::Mode::LoginFailed &&
      next.mode == OpenSkyAuthState::Mode::Account) {
    return "login ok — using account";
  }
  return {};
}
