// web_auth — the config web page's optional password (spec/done/01-ota-updates.md
// §5.7): password rules, what a settings-form submit does to the password,
// whether a request is let through, a lockout after repeated wrong
// passwords, and the "reset the password from the touchscreen" gesture.
//
// config_portal does the HTTP Digest part (WebServer::authenticate /
// requestAuthentication) and asks this module what to do with the answer.
//
// Pure — no WebServer / Arduino dependency — tested under `pio test -e native`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Digest login: one device, one user.
constexpr const char *kWebAuthUser = "admin";
constexpr const char *kWebAuthRealm = "CYD Sky Tracker";

constexpr size_t kWebPasswordMinLen = 8;
constexpr size_t kWebPasswordMaxLen = 64;

enum class WebPasswordCheck : uint8_t {
  Ok,        // includes "" = no password
  TooShort,  // 1..7 characters
  TooLong,   // > 64
  BadChar,   // outside printable ASCII 0x20..0x7E (keeps Digest and `curl -u` simple)
};
WebPasswordCheck validateWebPassword(const std::string &password);

// What one settings-form submit does to the stored password. The two "new
// password" fields are never pre-filled, so both empty = leave it alone.
struct PasswordChange {
  enum class Outcome : uint8_t {
    Unchanged,
    Set,       // `value` is the new password
    Cleared,   // "remove the password" ticked (wins over typed fields)
    Mismatch,  // the two fields differ — nothing saved
    Invalid,   // fails validateWebPassword() — `check` says why, nothing saved
  };
  Outcome outcome = Outcome::Unchanged;
  std::string value;
  WebPasswordCheck check = WebPasswordCheck::Ok;
};
PasswordChange decidePasswordChange(const std::string &newPassword, const std::string &repeat,
                                    bool clearRequested);

// Counts wrong passwords (any client): after kMaxFailures in a row every
// request is refused for kLockMs without looking at the password; a correct
// one resets the count. millis()-wraparound safe.
class AuthThrottle {
 public:
  static constexpr int kMaxFailures = 5;
  static constexpr uint32_t kLockMs = 60000;

  void onFailure(uint32_t nowMs);
  void onSuccess();
  bool isLocked(uint32_t nowMs) const;
  // Whole seconds until the lock ends (rounded up), 0 when not locked.
  uint32_t retryAfterS(uint32_t nowMs) const;
  int failures() const { return failures_; }

 private:
  int failures_ = 0;
  bool locked_ = false;
  uint32_t lockedAtMs_ = 0;
};

enum class AuthDecision : uint8_t {
  Allow,      // no password set, or the right one
  Challenge,  // 401 + a Digest challenge
  Locked,     // 429, too many wrong passwords
};
struct AuthOutcome {
  AuthDecision decision = AuthDecision::Allow;
  // A wrong password was actually sent (not just the browser's first,
  // credential-less request) — feed it to AuthThrottle::onFailure().
  bool countFailure = false;
  bool countSuccess = false;
};
AuthOutcome decideAuth(bool passwordSet, bool locked, bool sentCredentials, bool credentialsOk);

// ---- Cross-site requests (spec 10 §2.1) -------------------------------------
//
// Did a state-changing POST come from the panel's own page? `origin` /
// `referer` / `host` are the request headers as WebServer::header() returns
// them ("" = absent). Origin decides when present; without it, the host of
// Referer. Same = exact, case-insensitive host[:port] match against Host,
// scheme `http://` only, ":80" equal to no port. Neither header = NoHeaders
// (curl, `pio run -e cyd-ota -t upload`) — allowed.
enum class OriginCheck : uint8_t {
  SameOrigin,
  CrossOrigin,
  NoHeaders,
};
OriginCheck checkRequestOrigin(const std::string &origin, const std::string &referer,
                               const std::string &host);

// At UPLOAD_FILE_START, before anything is buffered or written: origin
// first, then the password.
enum class UploadStart : uint8_t {
  Accept,
  RefuseOrigin,  // 403, nothing written
  RefuseAuth,    // 401 / 429 as for any request
};
UploadStart decideUploadStart(OriginCheck origin, bool credentialsOk);

// "[auth] cross-origin POST /save from http://evil.example refused" — the
// Origin, or the Referer when there was no Origin.
std::string crossOriginLogLine(const std::string &method, const std::string &path,
                               const std::string &origin, const std::string &referer);

// Sent with every response of the panel (spec 10 §2.3): no framing; no
// Access-Control-* header at all (no CORS).
struct SecurityHeader {
  const char *name;
  const char *value;
};
constexpr size_t kSecurityHeaderCount = 2;
extern const SecurityHeader kSecurityHeaders[kSecurityHeaderCount];

// ---- Resetting a forgotten password from the touchscreen: hold a finger on
// the screen while the board powers up, for kResetHoldMs, then confirm
// YES / NO (no answer within kResetConfirmTimeoutMs = NO).
constexpr uint32_t kResetHoldMs = 5000;
constexpr uint32_t kResetConfirmTimeoutMs = 15000;
// The resistive panel drops a reading now and then mid-press; only a gap
// longer than this counts as letting go.
constexpr uint32_t kResetReleaseGraceMs = 300;

enum class ResetHoldState : uint8_t { Released, Holding, Done };
// `msSinceTouch`: time since the last sample that read "pressed";
// `heldMs`: time since the hold started.
ResetHoldState resetHoldState(uint32_t msSinceTouch, uint32_t heldMs);

enum class ResetChoice : uint8_t { None, Yes, No };
