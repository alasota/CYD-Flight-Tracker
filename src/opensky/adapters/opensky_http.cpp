// opensky_http — the device half of opensky_client: the OAuth2 token request,
// the /states/all GET (HTTP/1.0, parsed from the stream with
// extractAircraftFromStates()), 401 retry and 429 backoff, and the poll
// schedule. Hardware adapter (WiFiClientSecure / HTTPClient), not covered by
// Unity (CLAUDE.md "Testing"). See docs/data-sources.md "OpenSky".
#include "opensky/core/opensky_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>

#include "platform/net_config.h"

namespace {

constexpr char kTokenUrl[] =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
constexpr char kStatesUrlBase[] = "https://opensky-network.org/api/states/all";
constexpr char kRetryAfterHeader[] = "X-Rate-Limit-Retry-After-Seconds";
constexpr char kRemainingHeader[] = "X-Rate-Limit-Remaining";
constexpr uint32_t kDefaultRetryAfterS = 60;

// HTTP timeouts are shared across the networking modules — see net_config.h.

TokenState cachedToken;
uint32_t rateLimitedUntilMs = 0;
uint32_t lastPollMs = 0;
bool everPolled = false;
OpenSkyQuota quota;  // credits left / reset time, from the last answer's headers
bool fetchToken(const std::string &client_id, const std::string &client_secret, int *httpCode);

OpenSkyAuthState auth;  // did the login work (opensky_auth, spec 06 / 08)

// Moves `auth` to `next`, logging a login result once (not every poll, not
// on a credentials change — authChangeLogLine()).
void setAuth(const OpenSkyAuthState &next) {
  const std::string line = authChangeLogLine(auth, next);
  if (!line.empty()) Serial.printf("[opensky_client] %s\n", line.c_str());
  auth = next;
}

// One token request, with the outcome folded into `auth`. A rejected login
// drops the cached token too, so the retry 30 min later asks for a new one
// instead of reusing a token /states/all no longer accepts.
TokenOutcome fetchTokenTracked(const OpenSkyCredentials &creds) {
  int code = 0;
  const bool ok = fetchToken(creds.client_id, creds.client_secret, &code);
  const TokenOutcome outcome = classifyTokenResponse(code, ok);
  if (outcome == TokenOutcome::Rejected) cachedToken.valid = false;
  setAuth(onTokenResult(auth, outcome, code, millis()));
  return outcome;
}

// One /states/all attempt, with the 200-response body parsed inline
// straight from the socket (streaming + an ArduinoJson field filter) so a
// big busy-airspace response never has to exist in RAM as one contiguous
// String + std::string + document all at once — see docs/review-notes-2026.md 4.1.
struct StatesAttempt {
  bool transport_ok = false;  // got any HTTP response at all
  int code = 0;               // HTTP status when transport_ok
  bool body_ok = false;       // a 200 whose body parsed cleanly
  std::string retry_after;    // X-Rate-Limit-Retry-After-Seconds, if sent
  std::string remaining;      // X-Rate-Limit-Remaining, if sent
  std::vector<Aircraft> aircraft;
};

// `httpCode` gets the login server's status (0 = no answer) for opensky_auth.
bool fetchToken(const std::string &client_id, const std::string &client_secret, int *httpCode) {
  *httpCode = 0;
  WiFiClientSecure client;
  // No CA bundle pinned — acceptable for a hobby device per CLAUDE.md's
  // scope; a future hardening pass could pin OpenSky/Keycloak's cert.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kOpenSkyHttpTimeoutMs);
  if (!http.begin(client, kTokenUrl)) {
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see docs/review-notes-2026.md 4.2) — client_id/secret are short OAuth
  // client identifiers, so this buffer has generous headroom.
  std::string encodedId = urlEncode(client_id);
  std::string encodedSecret = urlEncode(client_secret);
  char body[512];
  int written =
      std::snprintf(body, sizeof(body), "grant_type=client_credentials&client_id=%s&client_secret=%s",
                    encodedId.c_str(), encodedSecret.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(body)) {
    Serial.println("[opensky_client] client_id/client_secret too long for token request buffer");
    http.end();
    return false;
  }

  int code = http.POST(reinterpret_cast<uint8_t *>(body), static_cast<size_t>(written));
  *httpCode = code > 0 ? code : 0;
  if (code != HTTP_CODE_OK) {
    Serial.printf("[opensky_client] token fetch failed (http=%d)\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok || doc["access_token"].isNull() ||
      doc["expires_in"].isNull()) {
    Serial.println("[opensky_client] token response missing access_token/expires_in");
    return false;
  }

  cachedToken.access_token = doc["access_token"].as<std::string>();
  cachedToken.expires_in_s = doc["expires_in"].as<uint32_t>();
  cachedToken.obtained_at_ms = millis();
  cachedToken.valid = true;
  return true;
}

StatesAttempt requestStatesOnce(const BoundingBox &bbox, const std::string &bearerToken) {
  StatesAttempt att;

  // Built into a fixed buffer instead of concatenating Arduino Strings
  // (see docs/review-notes-2026.md 4.2).
  char url[192];
  int urlWritten = std::snprintf(url, sizeof(url), "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
                                  kStatesUrlBase, static_cast<double>(bbox.lamin),
                                  static_cast<double>(bbox.lomin), static_cast<double>(bbox.lamax),
                                  static_cast<double>(bbox.lomax));
  if (urlWritten < 0 || static_cast<size_t>(urlWritten) >= sizeof(url)) {
    return att;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  // HTTP/1.0 so the server can't answer with Transfer-Encoding: chunked —
  // the body is parsed straight from getStream() below, which is the raw
  // socket: with chunking, ArduinoJson reads the leading hex chunk size as
  // a bare number, "succeeds", and every poll came back with 0 aircraft.
  http.useHTTP10(true);
  http.setConnectTimeout(kHttpConnectTimeoutMs);
  http.setTimeout(kOpenSkyHttpTimeoutMs);
  if (!http.begin(client, url)) {
    return att;
  }
  if (!bearerToken.empty()) {
    // std::string on the heap, not a 2KB stack buffer (docs/review-notes-2026.md 4.2) —
    // a bearer JWT is ~1KB and net_task's 16 KB stack also carries TLS.
    std::string authHeader = "Bearer ";
    authHeader += bearerToken;
    http.addHeader("Authorization", authHeader.c_str());
  }
  const char *collectHeaders[] = {kRetryAfterHeader, kRemainingHeader};
  http.collectHeaders(collectHeaders, 2);

  int code = http.GET();
  if (code <= 0) {
    http.end();
    return att;  // transport_ok stays false
  }

  att.transport_ok = true;
  att.code = code;
  if (http.hasHeader(kRetryAfterHeader)) {
    att.retry_after = std::string(http.header(kRetryAfterHeader).c_str());
  }
  if (http.hasHeader(kRemainingHeader)) {
    att.remaining = std::string(http.header(kRemainingHeader).c_str());
  }

  if (code == HTTP_CODE_OK) {
    int len = http.getSize();  // -1 when chunked / unknown
    if (len > 0 && static_cast<size_t>(len) > kMaxStatesResponseBytes) {
      Serial.printf("[opensky_client] /states/all body too large (%d bytes) — skipping parse\n",
                    len);
    } else {
      // Filter: keep every element of each state vector, drop the rest of
      // the envelope. Parsed directly from the socket stream.
      JsonDocument filter;
      filter["time"] = true;
      filter["states"][0] = true;
      JsonDocument doc;
      DeserializationError err =
          deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      if (err) {
        Serial.printf("[opensky_client] /states/all parse error: %s\n", err.c_str());
      } else if (doc["time"].isNull()) {
        // A real response always carries "time" ("states" alone may be
        // null when nothing is in range) — anything else is not the
        // envelope we expect, so don't report it as "0 aircraft".
        Serial.println("[opensky_client] /states/all response missing \"time\" - not a states body");
      } else {
        att.aircraft = extractAircraftFromStates(doc["states"].as<JsonArrayConst>());
        att.body_ok = true;
      }
    }
  }

  http.end();
  return att;
}

}  // namespace

OpenSkyPollResult fetchAircraftStates(const OpenSkyCredentials &creds, const Home &home) {
  OpenSkyPollResult result;
  uint32_t now = millis();

  if (rateLimitedUntilMs != 0 && isBeforeDeadline(now, rateLimitedUntilMs)) {
    int32_t remain_ms = static_cast<int32_t>(rateLimitedUntilMs - now);
    result.status = OpenSkyPollStatus::RateLimited;
    result.retry_after_s = remain_ms > 0 ? static_cast<uint32_t>((remain_ms + 999) / 1000) : 0;
    return result;
  }

  const bool haveCredentials = shouldUseOAuth(creds.client_id, creds.client_secret);
  const uint32_t hash = credentialsHash(creds.client_id, creds.client_secret);
  if (hash != auth.credentialsHash) cachedToken.valid = false;  // a token for the old ones
  setAuth(onCredentials(auth, hash, haveCredentials));

  // In LoginFailed the login server is asked only every kLoginRetryMs;
  // without a token this poll simply goes anonymous.
  bool tokenFetched = false;
  if (shouldTryToken(auth, now) && tokenNeedsRefresh(cachedToken, now)) {
    fetchTokenTracked(creds);
    tokenFetched = true;
  }

  BoundingBox bbox = computeBoundingBox(home.lat, home.lon, home.radius_deg);

  bool bearer = planStatesRequest(auth, cachedToken.valid);
  StatesAttempt att = requestStatesOnce(bbox, bearer ? cachedToken.access_token : std::string());

  // 401/403, decided by opensky_auth: with a token -> refresh once and retry
  // (a token fetched this poll is already fresh: that step is skipped);
  // still rejected -> LoginFailed and the same poll anonymously; rejected
  // anonymously -> AuthError below. At most two extra requests.
  for (int round = 0; att.transport_ok && round < 2; ++round) {
    const StatesDecision d = afterStatesResponse(auth, att.code, bearer, tokenFetched, millis());
    if (d.step == StatesStep::RefreshTokenAndRetry) {
      // Drop the rejected token first: if the refresh doesn't yield a new
      // one (rejected or no answer), the retry goes anonymous.
      cachedToken.valid = false;
      fetchTokenTracked(creds);
      tokenFetched = true;
    } else if (d.step == StatesStep::RetryAnonymous) {
      cachedToken.valid = false;
      setAuth(d.auth);
    } else {
      break;  // Accept, or AuthError (handled with the other 401/403 below)
    }
    bearer = planStatesRequest(auth, cachedToken.valid);
    att = requestStatesOnce(bbox, bearer ? cachedToken.access_token : std::string());
  }

  if (!att.transport_ok) {
    Serial.println("[opensky_client] /states/all request failed (no HTTP response)");
    result.status = OpenSkyPollStatus::NetworkError;
    return result;
  }

  quota = updateQuota(quota, att.code, att.remaining, att.retry_after, now);

  if (att.code == 429) {
    uint32_t backoff_s = parseRetryAfterSeconds(att.retry_after, kDefaultRetryAfterS);
    rateLimitedUntilMs = now + backoff_s * 1000UL;
    if (quota.reset_in_s > 0) {
      Serial.printf(
          "[opensky_client] rate limited (429): credits exhausted, pool renews in %us (~%s), "
          "next try in %us\n",
          static_cast<unsigned>(quota.reset_in_s), formatWait(quota.reset_in_s, Language::EN).c_str(),
          static_cast<unsigned>(backoff_s));
    } else {
      Serial.printf("[opensky_client] rate limited (429), backing off %us\n",
                    static_cast<unsigned>(backoff_s));
    }
    result.status = OpenSkyPollStatus::RateLimited;
    result.retry_after_s = backoff_s;
    return result;
  }

  if (att.code == 401 || att.code == 403) {
    // Rejected even without a token (a rejected token already fell back to
    // anonymous above) — surface it instead of silently going empty forever
    // (docs/review-notes-2026.md 1.3).
    Serial.printf("[opensky_client] /states/all rejected (http=%d, %s)\n", att.code,
                  bearer ? "account" : "anonymous");
    result.status = OpenSkyPollStatus::AuthError;
    return result;
  }

  if (att.code != 200 || !att.body_ok) {
    Serial.printf("[opensky_client] /states/all failed (http=%d, parsed=%d)\n", att.code,
                  att.body_ok ? 1 : 0);
    result.status = OpenSkyPollStatus::NetworkError;
    return result;
  }

  if (quota.state == OpenSkyQuota::State::Available) {
    Serial.printf("[opensky_client] ok: %u aircraft, credits remaining=%u (%s)\n",
                  static_cast<unsigned>(att.aircraft.size()), static_cast<unsigned>(quota.remaining),
                  bearer ? "account" : "anonymous");
  } else {
    Serial.printf("[opensky_client] ok: %u aircraft, credits remaining unknown (no header)\n",
                  static_cast<unsigned>(att.aircraft.size()));
  }

  result.status = OpenSkyPollStatus::Ok;
  result.aircraft = std::move(att.aircraft);
  return result;
}

OpenSkyQuota openSkyQuota() { return quota; }

OpenSkyAuthState openSkyAuthStatus(const OpenSkyCredentials &creds) {
  return onCredentials(auth, credentialsHash(creds.client_id, creds.client_secret),
                       shouldUseOAuth(creds.client_id, creds.client_secret));
}

uint32_t openSkyMsUntilNextPoll(uint32_t now_ms, const PollSettings &poll) {
  return msUntilNextPoll(now_ms, lastPollMs, everPolled, poll.interval_s * 1000UL);
}

OpenSkyPollResult openSkyClientPoll(uint32_t now_ms, const OpenSkyCredentials &creds,
                                    const Home &home, const PollSettings &poll) {
  if (!shouldPollNow(now_ms, lastPollMs, everPolled, poll.interval_s * 1000UL)) {
    return {};  // OpenSkyPollStatus::NotDue
  }

  OpenSkyPollResult result = fetchAircraftStates(creds, home);
  lastPollMs = now_ms;
  everPolled = true;
  return result;
}
