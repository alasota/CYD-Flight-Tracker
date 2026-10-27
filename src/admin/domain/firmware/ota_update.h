// ota_update — firmware updates over WiFi (spec/done/01-ota-updates.md): checks
// that an uploaded file really is this project's app image before the new
// slot is activated, drives the write through a small OtaWriter interface
// (config_portal plugs in arduino-esp32's Update), and decides when a freshly
// booted image counts as good (cancelling the bootloader's rollback).
//
// Pure — no Update / WebServer / Arduino dependency — tested under
// `pio test -e native`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "admin/app/web_i18n.h"  // WebStr

// ---- The image header (esp_image_header_t, first 24 bytes of an app .bin)
constexpr size_t kOtaImageHeaderBytes = 24;
constexpr uint8_t kOtaImageMagic = 0xE9;
constexpr uint16_t kOtaChipIdEsp32 = 0x0000;

enum class OtaImageCheck : uint8_t {
  Ok,
  TooShort,       // fewer than kOtaImageHeaderBytes
  LooksLikeFull,  // starts with 0xFF: the merged -full.bin (bootloader at 0x1000, padding before)
  NotAppImage,    // no 0xE9 magic byte
  WrongChip,      // chip_id (bytes 12..13, little-endian) isn't the plain ESP32
};
OtaImageCheck checkImageHeader(const uint8_t *data, size_t len);

// A real app image is ~1.3 MB; bootloader.bin is ~17 KB.
constexpr size_t kOtaMinImageBytes = 512 * 1024;

enum class OtaSizeCheck : uint8_t { Ok, Unknown, TooSmall, TooLarge };
// `declaredBytes` 0 = not known up front (plain form upload without ?size=).
OtaSizeCheck checkImageSize(size_t declaredBytes, size_t slotBytes);

// ---- The firmware marker: version.cpp puts
//   "CYDSKY-FW1|board=<board>|v=<version>"
// into every build. Finding it in the uploaded stream tells this firmware
// apart from any other ESP32 image. The prefix's own literal (here, in the
// scanner) also lands in the binary, followed straight by '\0' — that
// occurrence has no fields and is skipped.
constexpr const char *kFirmwareMarkerPrefix = "CYDSKY-FW1|";
constexpr size_t kFirmwareMarkerMaxBody = 64;

class FirmwareMarkerScanner {
 public:
  void feed(const uint8_t *data, size_t len);
  bool found() const { return found_; }
  const std::string &board() const { return board_; }
  const std::string &version() const { return version_; }

 private:
  void restart(char c);
  bool parseBody();

  size_t matched_ = 0;    // prefix characters matched so far
  bool inBody_ = false;   // prefix done, collecting up to '\0'
  std::string body_;
  bool found_ = false;
  std::string board_;
  std::string version_;
};

// 0..100, or -1 when the total isn't known.
int otaProgressPercent(size_t written, size_t total);

enum class OtaResult : uint8_t {
  Ok,
  NoFile,         // nothing uploaded
  TooShort,
  LooksLikeFull,
  NotAppImage,
  WrongChip,
  TooSmall,
  TooLarge,
  NotSkyTracker,  // no marker anywhere in the file
  WriteFailed,    // the writer refused data (flash error, or more than the slot holds)
  VerifyFailed,   // finishing failed (image checksum / SHA-256 didn't verify)
  Aborted,        // the upload broke off
  Unauthorized,   // password set, request not logged in (nothing was written)
  // Download from GitHub (release_check):
  ChecksumMismatch,  // the file's SHA-256 isn't the one GitHub lists for it
  VersionMismatch,   // the marker's version isn't the release's tag
  DownloadFailed,    // connection / HTTP error, or the size GitHub gave didn't match
};
constexpr int kOtaResultCount = 16;
WebStr otaResultMessage(OtaResult r);
// HTTP status for the /update response.
int otaResultHttpStatus(OtaResult r);

// What config_portal's Update adapter does — a seam for the native tests.
class OtaWriter {
 public:
  virtual ~OtaWriter() = default;
  virtual bool begin(size_t sizeOrZero) = 0;  // 0 = size unknown
  virtual size_t write(const uint8_t *data, size_t len) = 0;
  virtual bool end() = 0;  // verifies the image and switches the boot slot
  virtual void abort() = 0;
};

// SHA-256 of everything an upload delivers — a seam like OtaWriter
// (mbedTLS on the board, a fake in the native tests).
class OtaDigest {
 public:
  virtual ~OtaDigest() = default;
  virtual void reset() = 0;
  virtual void update(const uint8_t *data, size_t len) = 0;
  virtual std::string finishHex() = 0;  // 64 lower-case hex
};

// What a download from GitHub must also match (empty = not checked — a
// file picked by hand on the config page).
struct OtaExpectations {
  std::string sha256;   // 64 lower-case hex
  std::string version;  // the release tag, compared with the marker's v=
};

// One upload, chunk by chunk: start() -> feed()* -> finish() (or abort()).
// Nothing reaches the writer until the header checks out; finish() only
// calls writer.end() — which activates the new slot — when the marker was
// found (and, for a download, its version and the file's SHA-256 match).
// On any failure the writer is aborted and the rest is ignored.
class OtaSession {
 public:
  OtaSession(OtaWriter &writer, size_t slotBytes, OtaDigest *digest = nullptr)
      : writer_(writer), slotBytes_(slotBytes), digest_(digest) {}

  void start(size_t declaredBytes, const OtaExpectations &expect = OtaExpectations());
  void feed(const uint8_t *data, size_t len);
  OtaResult finish();
  void abort();

  bool started() const { return started_; }
  bool inProgress() const { return started_ && !finished_; }
  bool failed() const { return failed_; }
  OtaResult result() const { return result_; }
  size_t received() const { return received_; }
  size_t declaredBytes() const { return declared_; }
  const FirmwareMarkerScanner &marker() const { return scanner_; }

 private:
  void fail(OtaResult r);
  void writeOut(const uint8_t *data, size_t len);

  OtaWriter &writer_;
  size_t slotBytes_;
  size_t declared_ = 0;
  size_t received_ = 0;
  bool started_ = false;
  bool begun_ = false;  // writer.begin() done
  bool failed_ = false;
  bool finished_ = false;
  OtaResult result_ = OtaResult::NoFile;
  std::string header_;  // first bytes, held until there are enough to check
  FirmwareMarkerScanner scanner_;
  OtaDigest *digest_;
  OtaExpectations expect_;
};

// ---- After an update: the bootloader boots the new image "pending
// verify"; if it restarts before the image is marked valid it rolls back.
// Mark it valid once it has run kOtaValidAfterMs and can be reached to
// flash a fix (web page up, or the WiFi setup hotspot) — and the UI loop on
// the other core is alive (`uiAlive`: its heartbeat is fresh), so an image
// whose display hangs while the network works still rolls back
// (spec/done/04-dual-core.md). OpenSky trouble is deliberately not part of this.
constexpr uint32_t kOtaValidAfterMs = 30000;
bool shouldMarkAppValid(bool pendingVerify, uint32_t uptimeMs, bool reachable, bool uiAlive);

// A new image whose UI loop has stopped (no heartbeat) while still on
// probation: restart it, so the bootloader rolls back right away instead of
// the board sitting on a frozen screen until someone pulls the plug. Only
// after kOtaValidAfterMs (the UI gets a fair start) and never once valid.
bool shouldRestartForRollback(bool pendingVerify, uint32_t uptimeMs, bool uiAlive);

#ifdef ARDUINO
// The one session that writes to the spare app slot (arduino-esp32 Update +
// mbedTLS SHA-256), shared by the config page's POST /update and
// release_check's download — so two updates can never run at once.
OtaSession &otaFlashSession();
// Update.errorString() of the last failed write / verify.
const char *otaFlashError();
#endif
