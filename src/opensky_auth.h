// opensky_auth — did the OpenSky login work? The state device_alerts reads
// for the "login failed" alert (spec/done/08-device-alerts.md §2) and the
// daily credit limit that goes with it.
//
// This is the first slice of spec/planned/06-opensky-login-fallback.md: the
// state and its transitions on a token answer. Still to come with spec 06:
// waiting 30 min before trying a rejected login again (today every poll
// tries), the OpenSky tab's red pill and the anonymous retry of a poll.
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
};

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

// After one token request: 200 with a token -> Account; 400 / 401 / 403 ->
// LoginFailed (+ the code); anything else (no answer, 5xx, a body without a
// token) is transient -> unchanged.
OpenSkyAuthState onTokenResult(const OpenSkyAuthState &s, int httpCode, bool gotToken);
