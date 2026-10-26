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

// TODO(spec 10): stub from test-author
OriginCheck checkRequestOrigin(const std::string & /*origin*/, const std::string & /*referer*/,
                               const std::string & /*host*/) {
  return OriginCheck::NoHeaders;
}

// TODO(spec 10): stub from test-author
UploadStart decideUploadStart(OriginCheck /*origin*/, bool /*credentialsOk*/) {
  return UploadStart::Accept;
}

// TODO(spec 10): stub from test-author
std::string crossOriginLogLine(const std::string & /*method*/, const std::string & /*path*/,
                               const std::string & /*origin*/, const std::string & /*referer*/) {
  return {};
}

// TODO(spec 10): stub from test-author
const SecurityHeader kSecurityHeaders[kSecurityHeaderCount] = {{"", ""}, {"", ""}};

ResetHoldState resetHoldState(uint32_t msSinceTouch, uint32_t heldMs) {
  if (msSinceTouch > kResetReleaseGraceMs) return ResetHoldState::Released;
  return heldMs >= kResetHoldMs ? ResetHoldState::Done : ResetHoldState::Holding;
}
