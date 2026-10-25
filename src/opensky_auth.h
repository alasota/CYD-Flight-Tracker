// opensky_auth — did the OpenSky login work? The state device_alerts reads
// for the "login failed" alert (spec/done/08-device-alerts.md §2), the daily
// credit limit that goes with it, and the decisions opensky_client takes
// around a poll (spec/done/06-opensky-login-fallback.md): wait 30 min
// before asking the login server again after a rejection, send a Bearer token
// only with a working account, and retry a poll rejected with a token
// anonymously instead of giving up.
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <cstdint>
#include <string>

struct OpenSkyAuthState {
  enum class Mode : uint8_t {
    NoCredentials,  // client_id / secret not set: anonymous on purpose
    Account,        // credentials set and not rejected (yet)
    LoginFailed,    // the login server rejected them: running anonymously
  };
  Mode mode = Mode::NoCredentials;
  int lastHttpCode = 0;          // the login server's answer when it rejected them
  uint32_t credentialsHash = 0;  // which credentials this state is about
  uint32_t failedSinceMs = 0;    // millis() of the first rejection of this series
  uint32_t nextTokenTryMs = 0;   // LoginFailed: don't ask the login server before this
  bool tokenGranted = false;     // a token was granted for these credentials, none rejected since
  bool failedSinceStale = false;  // LoginFailed for so long that failedSinceMs may have wrapped
};

// How long a rejected login waits before the login server is asked again.
constexpr uint32_t kLoginRetryMs = 30u * 60u * 1000u;

// failedSinceMs is a millis() value, which wraps after ~49.7 days; past this
// age the "since" time is no longer trusted (the series is marked stale at
// the next token result, so it stays hidden after the wrap too).
constexpr uint32_t kLoginFailedSinceMaxMs = 40u * 24u * 60u * 60u * 1000u;

// Is failedSinceMs still a reliable "since when" at nowMs? Only in
// LoginFailed, not stale, and at most kLoginFailedSinceMaxMs ago.
bool loginFailedSinceKnown(const OpenSkyAuthState &s, uint32_t nowMs);

// What one answer of the login server means.
enum class TokenOutcome : uint8_t {
  Ok,         // 200 with an access_token
  Rejected,   // 400 / 401 / 403: wrong client_id / secret
  Transient,  // no answer, timeout, 5xx, 200 without a token
};
TokenOutcome classifyTokenResponse(int httpCode, bool hasAccessToken);

// OpenSky's daily credit allowance (CLAUDE.md "Rate limits"): 4000 with an
// account, 400 anonymous — and in LoginFailed, which runs anonymously.
constexpr uint32_t kOpenSkyAccountDailyLimit = 4000;
constexpr uint32_t kOpenSkyAnonymousDailyLimit = 400;
uint32_t openSkyDailyLimit(const OpenSkyAuthState &s);

// FNV-1a over id, a 0x1F separator and secret — notices a credentials change
// without keeping a copy of the secret around.
uint32_t credentialsHash(const std::string &id, const std::string &secret);

// The state for the credentials in the config: none -> NoCredentials; other
// credentials than last time -> Account (the new ones get a fair try);
// the same ones -> unchanged (a LoginFailed stays until they change or a
// token is granted).
OpenSkyAuthState onCredentials(const OpenSkyAuthState &s, uint32_t hash, bool haveCredentials);

// After one token request (spec 06 §3.1): Ok -> Account + tokenGranted (code,
// failedSinceMs, nextTokenTryMs cleared); Rejected -> LoginFailed + code,
// tokenGranted cleared, failedSinceMs = now unless already failed,
// nextTokenTryMs = now + kLoginRetryMs; Transient in
// Account -> unchanged; Transient in LoginFailed -> nextTokenTryMs = now +
// kLoginRetryMs. NoCredentials never changes. A token result in LoginFailed
// older than kLoginFailedSinceMaxMs marks failedSinceStale (cleared by Ok and
// by the first rejection of a new series).
OpenSkyAuthState onTokenResult(const OpenSkyAuthState &s, TokenOutcome o, int httpCode,
                               uint32_t nowMs);

// May the login server be asked now? NoCredentials: never; Account: yes;
// LoginFailed: once nextTokenTryMs has passed (millis() wrap-safe).
bool shouldTryToken(const OpenSkyAuthState &s, uint32_t nowMs);

// Send /states/all with "Authorization: Bearer"? Only in Account with a valid token.
bool planStatesRequest(const OpenSkyAuthState &s, bool tokenValid);

// What to do after a /states/all answer:
//   not 401/403                        -> Accept, auth unchanged
//   401/403, Bearer, not refreshed yet -> RefreshTokenAndRetry, auth unchanged
//   401/403, Bearer, already refreshed -> RetryAnonymous, auth = onTokenResult(Rejected)
//   401/403, anonymous                 -> AuthError, auth unchanged
enum class StatesStep : uint8_t { Accept, RefreshTokenAndRetry, RetryAnonymous, AuthError };
struct StatesDecision {
  StatesStep step;
  OpenSkyAuthState auth;
};
StatesDecision afterStatesResponse(const OpenSkyAuthState &s, int httpCode, bool usedBearer,
                                   bool tokenRefreshedThisPoll, uint32_t nowMs);

// The log line for a login result (without the "[opensky_client] " prefix),
// "" otherwise:
//   -> LoginFailed: "login failed (http=401) — check client_id/secret; using
//                    anonymous access, next try in 30 min"
//   the first token granted for these credentials (after a rejection, a
//   credentials change or a boot): "login ok — using account"
// Setting up, changing or removing credentials (onCredentials) is not a
// login result and logs nothing, nor does a later token refresh.
std::string authChangeLogLine(const OpenSkyAuthState &prev, const OpenSkyAuthState &next);
