#include "web_auth.h"

WebPasswordCheck validateWebPassword(const std::string &password) {
  if (password.empty()) return WebPasswordCheck::Ok;
  for (char c : password) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u > 0x7E) return WebPasswordCheck::BadChar;
  }
  if (password.size() < kWebPasswordMinLen) return WebPasswordCheck::TooShort;
  if (password.size() > kWebPasswordMaxLen) return WebPasswordCheck::TooLong;
  return WebPasswordCheck::Ok;
}

PasswordChange decidePasswordChange(const std::string &newPassword, const std::string &repeat,
                                    bool clearRequested) {
  PasswordChange c;
  if (clearRequested) {
    c.outcome = PasswordChange::Outcome::Cleared;
    return c;
  }
  if (newPassword.empty() && repeat.empty()) return c;  // Unchanged
  if (newPassword != repeat) {
    c.outcome = PasswordChange::Outcome::Mismatch;
    return c;
  }
  c.check = validateWebPassword(newPassword);
  if (c.check != WebPasswordCheck::Ok) {
    c.outcome = PasswordChange::Outcome::Invalid;
    return c;
  }
  c.outcome = PasswordChange::Outcome::Set;
  c.value = newPassword;
  return c;
}

void AuthThrottle::onFailure(uint32_t nowMs) {
  if (locked_) {
    if (isLocked(nowMs)) return;  // already refusing; don't extend the lock
    locked_ = false;
  }
  if (++failures_ >= kMaxFailures) {
    failures_ = 0;
    locked_ = true;
    lockedAtMs_ = nowMs;
  }
}

void AuthThrottle::onSuccess() {
  failures_ = 0;
  locked_ = false;
}

bool AuthThrottle::isLocked(uint32_t nowMs) const {
  return locked_ && (nowMs - lockedAtMs_) < kLockMs;
}

uint32_t AuthThrottle::retryAfterS(uint32_t nowMs) const {
  if (!isLocked(nowMs)) return 0;
  const uint32_t leftMs = kLockMs - (nowMs - lockedAtMs_);
  return (leftMs + 999) / 1000;
}

AuthOutcome decideAuth(bool passwordSet, bool locked, bool sentCredentials, bool credentialsOk) {
  AuthOutcome o;
  if (!passwordSet) return o;  // Allow
  if (locked) {
    o.decision = AuthDecision::Locked;
    return o;
  }
  if (sentCredentials && credentialsOk) {
    o.countSuccess = true;
    return o;  // Allow
  }
  o.decision = AuthDecision::Challenge;
  o.countFailure = sentCredentials;
  return o;
}

namespace {

// "http://Host:80/path?q#f" -> "host" (lowercased, ":80" dropped). Returns ""
// when it isn't a plain http URL with a host: other scheme, no scheme,
// "null", userinfo ("http://cyd-sky.local@evil.example" names evil.example),
// or — with `allowTail` false (Origin) — anything after the authority.
std::string httpAuthority(const std::string &url, bool allowTail) {
  static const char kScheme[] = "http://";
  const size_t schemeLen = sizeof(kScheme) - 1;
  if (url.size() <= schemeLen) return {};
  for (size_t i = 0; i < schemeLen; ++i) {
    const char c = url[i];
    const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    if (lower != kScheme[i]) return {};
  }
  const size_t end = url.find_first_of("/?#", schemeLen);
  if (end != std::string::npos && !allowTail) return {};
  return url.substr(schemeLen, end == std::string::npos ? std::string::npos : end - schemeLen);
}

// host[:port] as compared: lowercase, ":80" = no port. "" = unusable.
std::string normalizeHost(std::string authority) {
  if (authority.empty() || authority.find('@') != std::string::npos) return {};
  for (char &c : authority) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  static const char kDefaultPort[] = ":80";
  const size_t portLen = sizeof(kDefaultPort) - 1;
  if (authority.size() > portLen &&
      authority.compare(authority.size() - portLen, portLen, kDefaultPort) == 0) {
    authority.erase(authority.size() - portLen);
  }
  return authority;
}

}  // namespace

OriginCheck checkRequestOrigin(const std::string &origin, const std::string &referer,
                               const std::string &host) {
  std::string from;
  if (!origin.empty()) {
    from = normalizeHost(httpAuthority(origin, /*allowTail=*/false));
  } else if (!referer.empty()) {
    from = normalizeHost(httpAuthority(referer, /*allowTail=*/true));
  } else {
    return OriginCheck::NoHeaders;
  }
  const std::string own = normalizeHost(host);
  if (from.empty() || own.empty()) return OriginCheck::CrossOrigin;
  return from == own ? OriginCheck::SameOrigin : OriginCheck::CrossOrigin;
}

UploadStart decideUploadStart(OriginCheck origin, bool credentialsOk) {
  if (origin == OriginCheck::CrossOrigin) return UploadStart::RefuseOrigin;
  return credentialsOk ? UploadStart::Accept : UploadStart::RefuseAuth;
}

std::string crossOriginLogLine(const std::string &method, const std::string &path,
                               const std::string &origin, const std::string &referer) {
  const std::string &from = !origin.empty() ? origin : referer;
  return "[auth] cross-origin " + method + " " + path + " from " +
         (from.empty() ? std::string("(none)") : from) + " refused";
}

const SecurityHeader kSecurityHeaders[kSecurityHeaderCount] = {
    {"X-Frame-Options", "DENY"},
    {"Content-Security-Policy", "frame-ancestors 'none'"},
};

ResetHoldState resetHoldState(uint32_t msSinceTouch, uint32_t heldMs) {
  if (msSinceTouch > kResetReleaseGraceMs) return ResetHoldState::Released;
  return heldMs >= kResetHoldMs ? ResetHoldState::Done : ResetHoldState::Holding;
}
