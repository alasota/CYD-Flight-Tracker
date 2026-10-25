#include "hexdb_http.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "net_config.h"  // shared HTTP timeouts (review notes 5.1)

namespace {

constexpr const char *kHexdbHost = "hexdb.io";
// One TLS session per batch (see the header); false = a handshake per request.
constexpr bool kHexdbKeepAlive = true;

// No CA bundle pinned — same rationale as opensky_client: acceptable for a
// hobby device, not hardened. Both live for the whole batch: HTTPClient's
// destructor stop()s the socket, so a per-request HTTPClient would close the
// kept-alive connection after every lookup.
WiFiClientSecure *client = nullptr;
HTTPClient *http = nullptr;

void closeClient() {
  delete http;  // stops the socket
  http = nullptr;
  if (client != nullptr) client->stop();
  delete client;
  client = nullptr;
}

// One attempt on the current (or a new) connection.
HexdbHttpResult getOnce(const char *url, bool *reused) {
  HexdbHttpResult r;
  *reused = client != nullptr && http != nullptr && client->connected();
  const uint32_t t0 = millis();
  if (!*reused) {
    closeClient();
    client = new WiFiClientSecure();
    client->setInsecure();
    if (!client->connect(kHexdbHost, 443, kHttpConnectTimeoutMs)) {
      Serial.printf("[hexdb] connect failed (%u ms)\n", static_cast<unsigned>(millis() - t0));
      closeClient();
      return r;
    }
    http = new HTTPClient();
    http->setReuse(kHexdbKeepAlive);
    http->setConnectTimeout(kHttpConnectTimeoutMs);
    http->setTimeout(kHexdbHttpTimeoutMs);
  }
  const uint32_t t1 = millis();

  if (!http->begin(*client, url)) {
    closeClient();
    return r;
  }
  r.code = http->GET();
  if (r.code > 0) r.body = std::string(http->getString().c_str());
  http->end();  // with reuse: keeps the socket open for the next request
  Serial.printf("[hexdb] %s -> %d in %u ms (%s %u ms)\n", url + 16, r.code,
                static_cast<unsigned>(millis() - t0), *reused ? "reused," : "handshake",
                static_cast<unsigned>(t1 - t0));
  if (!kHexdbKeepAlive || r.code <= 0) closeClient();
  return r;
}

}  // namespace

HexdbHttpResult hexdbGet(const char *url) {
  bool reused = false;
  HexdbHttpResult r = getOnce(url, &reused);
  // The server may close an idle kept-alive connection between two requests:
  // that shows up as a transport error on a reused socket — once more, fresh.
  if (r.code <= 0 && reused) r = getOnce(url, &reused);
  return r;
}

void hexdbSessionEnd() { closeClient(); }

#endif  // ARDUINO
