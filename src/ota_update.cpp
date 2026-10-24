#include "ota_update.h"

#include <cstring>

OtaImageCheck checkImageHeader(const uint8_t *data, size_t len) {
  if (data == nullptr || len < kOtaImageHeaderBytes) return OtaImageCheck::TooShort;
  if (data[0] == 0xFF) return OtaImageCheck::LooksLikeFull;
  if (data[0] != kOtaImageMagic) return OtaImageCheck::NotAppImage;
  const uint16_t chipId = static_cast<uint16_t>(data[12] | (data[13] << 8));
  if (chipId != kOtaChipIdEsp32) return OtaImageCheck::WrongChip;
  return OtaImageCheck::Ok;
}

OtaSizeCheck checkImageSize(size_t declaredBytes, size_t slotBytes) {
  if (declaredBytes == 0) return OtaSizeCheck::Unknown;
  if (declaredBytes < kOtaMinImageBytes) return OtaSizeCheck::TooSmall;
  if (declaredBytes > slotBytes) return OtaSizeCheck::TooLarge;
  return OtaSizeCheck::Ok;
}

// ---- FirmwareMarkerScanner

void FirmwareMarkerScanner::restart(char c) {
  inBody_ = false;
  body_.clear();
  // The prefix has no repeated 'C', so a failed match can only restart here.
  matched_ = (c == kFirmwareMarkerPrefix[0]) ? 1 : 0;
}

bool FirmwareMarkerScanner::parseBody() {
  std::string board, version;
  size_t pos = 0;
  while (pos <= body_.size()) {
    size_t bar = body_.find('|', pos);
    if (bar == std::string::npos) bar = body_.size();
    const std::string field = body_.substr(pos, bar - pos);
    if (field.rfind("board=", 0) == 0) board = field.substr(6);
    if (field.rfind("v=", 0) == 0) version = field.substr(2);
    pos = bar + 1;
  }
  if (board.empty() || version.empty()) return false;
  board_ = board;
  version_ = version;
  return true;
}

void FirmwareMarkerScanner::feed(const uint8_t *data, size_t len) {
  static const size_t kPrefixLen = std::strlen(kFirmwareMarkerPrefix);
  for (size_t i = 0; i < len && !found_; ++i) {
    const char c = static_cast<char>(data[i]);
    if (inBody_) {
      if (c == '\0') {
        found_ = parseBody();
        restart('\0');
      } else if (body_.size() >= kFirmwareMarkerMaxBody) {
        restart(c);
      } else {
        body_ += c;
      }
    } else if (c == kFirmwareMarkerPrefix[matched_]) {
      if (++matched_ == kPrefixLen) {
        inBody_ = true;
        matched_ = 0;
      }
    } else {
      restart(c);
    }
  }
}

int otaProgressPercent(size_t written, size_t total) {
  if (total == 0) return -1;
  if (written >= total) return 100;
  return static_cast<int>(static_cast<uint64_t>(written) * 100u / total);
}

WebStr otaResultMessage(OtaResult r) {
  switch (r) {
    case OtaResult::Ok:
      return WebStr::OtaOk;
    case OtaResult::NoFile:
      return WebStr::OtaNoFile;
    case OtaResult::LooksLikeFull:
      return WebStr::OtaFullImage;
    case OtaResult::TooShort:
    case OtaResult::NotAppImage:
    case OtaResult::TooSmall:
      return WebStr::OtaNotFirmware;
    case OtaResult::WrongChip:
      return WebStr::OtaWrongChip;
    case OtaResult::TooLarge:
      return WebStr::OtaTooLarge;
    case OtaResult::NotSkyTracker:
      return WebStr::OtaNotSkyTracker;
    case OtaResult::WriteFailed:
      return WebStr::OtaWriteFailed;
    case OtaResult::VerifyFailed:
      return WebStr::OtaVerifyFailed;
    case OtaResult::Unauthorized:
      return WebStr::OtaUnauthorized;
    case OtaResult::Aborted:
    default:
      return WebStr::OtaAborted;
  }
}

int otaResultHttpStatus(OtaResult r) {
  switch (r) {
    case OtaResult::Ok:
      return 200;
    case OtaResult::TooLarge:
      return 413;
    case OtaResult::WriteFailed:
    case OtaResult::VerifyFailed:
      return 500;
    case OtaResult::Unauthorized:
      return 401;
    default:
      return 400;
  }
}

// ---- OtaSession

void OtaSession::start(size_t declaredBytes) {
  declared_ = declaredBytes;
  received_ = 0;
  started_ = true;
  begun_ = false;
  failed_ = false;
  finished_ = false;
  result_ = OtaResult::NoFile;
  header_.clear();
  scanner_ = FirmwareMarkerScanner();

  switch (checkImageSize(declaredBytes, slotBytes_)) {
    case OtaSizeCheck::TooSmall:
      fail(OtaResult::TooSmall);
      break;
    case OtaSizeCheck::TooLarge:
      fail(OtaResult::TooLarge);
      break;
    default:
      break;
  }
}

void OtaSession::fail(OtaResult r) {
  if (failed_) return;
  failed_ = true;
  result_ = r;
  if (begun_) writer_.abort();
}

void OtaSession::writeOut(const uint8_t *data, size_t len) {
  if (len == 0) return;
  if (writer_.write(data, len) != len) fail(OtaResult::WriteFailed);
}

void OtaSession::feed(const uint8_t *data, size_t len) {
  if (!started_ || failed_ || finished_ || len == 0) return;
  received_ += len;
  scanner_.feed(data, len);

  if (begun_) {
    writeOut(data, len);
    return;
  }

  // Hold the first bytes until the header can be checked.
  header_.append(reinterpret_cast<const char *>(data), len);
  if (header_.size() < kOtaImageHeaderBytes) return;

  switch (checkImageHeader(reinterpret_cast<const uint8_t *>(header_.data()), header_.size())) {
    case OtaImageCheck::Ok:
      break;
    case OtaImageCheck::LooksLikeFull:
      return fail(OtaResult::LooksLikeFull);
    case OtaImageCheck::WrongChip:
      return fail(OtaResult::WrongChip);
    case OtaImageCheck::TooShort:
      return fail(OtaResult::TooShort);
    case OtaImageCheck::NotAppImage:
    default:
      return fail(OtaResult::NotAppImage);
  }
  if (!writer_.begin(declared_)) return fail(OtaResult::WriteFailed);
  begun_ = true;
  writeOut(reinterpret_cast<const uint8_t *>(header_.data()), header_.size());
  header_.clear();
  header_.shrink_to_fit();
}

OtaResult OtaSession::finish() {
  if (finished_) return result_;
  finished_ = true;
  if (failed_) return result_;
  if (received_ == 0) {
    result_ = OtaResult::NoFile;
    failed_ = true;
    return result_;
  }
  if (!begun_) {  // fewer bytes than a header
    fail(OtaResult::TooShort);
    return result_;
  }
  if (received_ < kOtaMinImageBytes) {
    fail(OtaResult::TooSmall);
    return result_;
  }
  if (!scanner_.found()) {
    fail(OtaResult::NotSkyTracker);
    return result_;
  }
  if (!writer_.end()) {
    failed_ = true;  // end() already gave up; no abort() after it
    result_ = OtaResult::VerifyFailed;
    return result_;
  }
  result_ = OtaResult::Ok;
  return result_;
}

void OtaSession::abort() {
  if (!started_ || finished_) return;
  fail(OtaResult::Aborted);
  finished_ = true;
}

bool shouldMarkAppValid(bool pendingVerify, uint32_t uptimeMs, bool reachable) {
  return pendingVerify && reachable && uptimeMs >= kOtaValidAfterMs;
}
