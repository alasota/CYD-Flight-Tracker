// ota_update: image header, marker scanner, OtaSession. CLAUDE.md "Firmware updates (OTA)", spec/01.
#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ota_update.h"

namespace {

namespace {

std::vector<uint8_t> esp32Header() {
  std::vector<uint8_t> h(kOtaImageHeaderBytes, 0);
  h[0] = 0xE9;  // magic
  h[1] = 6;     // segment count
  return h;
}

const char kMarker[] = "CYDSKY-FW1|board=cyd-r|v=v1.3.0";

// A fake app image: valid header, filler, the marker (with its '\0'), filler.
std::vector<uint8_t> fakeImage(size_t size, bool withMarker = true, size_t markerAt = 4096) {
  std::vector<uint8_t> img = esp32Header();
  img.resize(size, 0x5A);
  if (withMarker) std::memcpy(img.data() + markerAt, kMarker, sizeof(kMarker));
  return img;
}

struct FakeWriter : OtaWriter {
  bool beginOk = true;
  bool endOk = true;
  size_t failWriteAfter = SIZE_MAX;  // accept only this many bytes in total
  int begins = 0, ends = 0, aborts = 0;
  size_t beginSize = 123;
  std::vector<uint8_t> data;

  bool begin(size_t size) override {
    ++begins;
    beginSize = size;
    return beginOk;
  }
  size_t write(const uint8_t *d, size_t len) override {
    if (data.size() + len > failWriteAfter) return 0;
    data.insert(data.end(), d, d + len);
    return len;
  }
  bool end() override {
    ++ends;
    return endOk;
  }
  void abort() override { ++aborts; }
};

constexpr size_t kSlot = 0x1E0000;  // min_spiffs.csv app slot (1.875 MB)

// Feeds `img` in chunks of `chunk` bytes.
OtaResult upload(OtaSession &s, const std::vector<uint8_t> &img, size_t chunk, size_t declared) {
  s.start(declared);
  for (size_t off = 0; off < img.size(); off += chunk) {
    s.feed(img.data() + off, std::min(chunk, img.size() - off));
  }
  return s.finish();
}

void scan(FirmwareMarkerScanner &sc, const std::string &s) {
  sc.feed(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

}  // namespace

// ---- checkImageHeader

static void test_header_ok_for_esp32_app(void) {
  std::vector<uint8_t> h = esp32Header();
  TEST_ASSERT_TRUE(checkImageHeader(h.data(), h.size()) == OtaImageCheck::Ok);
}

static void test_header_recognises_the_full_image(void) {
  std::vector<uint8_t> h(64, 0xFF);
  TEST_ASSERT_TRUE(checkImageHeader(h.data(), h.size()) == OtaImageCheck::LooksLikeFull);
}

static void test_header_rejects_other_chips_and_junk(void) {
  std::vector<uint8_t> h = esp32Header();
  h[12] = 0x09;  // ESP32-S3
  TEST_ASSERT_TRUE(checkImageHeader(h.data(), h.size()) == OtaImageCheck::WrongChip);
  const uint8_t png[kOtaImageHeaderBytes] = {0x89, 'P', 'N', 'G'};
  TEST_ASSERT_TRUE(checkImageHeader(png, sizeof(png)) == OtaImageCheck::NotAppImage);
  TEST_ASSERT_TRUE(checkImageHeader(h.data(), 10) == OtaImageCheck::TooShort);
  TEST_ASSERT_TRUE(checkImageHeader(nullptr, 0) == OtaImageCheck::TooShort);
}

// ---- checkImageSize

static void test_size_checks(void) {
  TEST_ASSERT_TRUE(checkImageSize(0, kSlot) == OtaSizeCheck::Unknown);
  TEST_ASSERT_TRUE(checkImageSize(17 * 1024, kSlot) == OtaSizeCheck::TooSmall);
  TEST_ASSERT_TRUE(checkImageSize(kOtaMinImageBytes, kSlot) == OtaSizeCheck::Ok);
  TEST_ASSERT_TRUE(checkImageSize(kSlot, kSlot) == OtaSizeCheck::Ok);
  TEST_ASSERT_TRUE(checkImageSize(kSlot + 1, kSlot) == OtaSizeCheck::TooLarge);
}

// ---- FirmwareMarkerScanner

static void test_marker_found_in_one_piece(void) {
  FirmwareMarkerScanner sc;
  scan(sc, std::string("junk") + kMarker + std::string(1, '\0') + "more");
  TEST_ASSERT_TRUE(sc.found());
  TEST_ASSERT_EQUAL_STRING("cyd-r", sc.board().c_str());
  TEST_ASSERT_EQUAL_STRING("v1.3.0", sc.version().c_str());
}

static void test_marker_found_when_split_at_every_point(void) {
  const std::string stream = std::string("xxCYD") + kMarker + std::string(1, '\0') + "yy";
  for (size_t cut = 0; cut <= stream.size(); ++cut) {
    FirmwareMarkerScanner sc;
    scan(sc, stream.substr(0, cut));
    scan(sc, stream.substr(cut));
    TEST_ASSERT_TRUE_MESSAGE(sc.found(), std::to_string(cut).c_str());
    TEST_ASSERT_EQUAL_STRING("v1.3.0", sc.version().c_str());
  }
}

static void test_bare_prefix_is_skipped(void) {
  // The scanner's own prefix literal sits in the binary followed by '\0'.
  FirmwareMarkerScanner sc;
  scan(sc, std::string(kFirmwareMarkerPrefix) + std::string(1, '\0'));
  TEST_ASSERT_FALSE(sc.found());
  scan(sc, std::string(kMarker) + std::string(1, '\0'));
  TEST_ASSERT_TRUE(sc.found());
}

static void test_marker_needs_terminator_and_fields(void) {
  FirmwareMarkerScanner a;
  scan(a, kMarker);  // no '\0' yet
  TEST_ASSERT_FALSE(a.found());

  FirmwareMarkerScanner b;
  scan(b, std::string("CYDSKY-FW1|v=v1.0.0") + std::string(1, '\0'));
  TEST_ASSERT_FALSE(b.found());  // no board
}

static void test_overlong_body_is_ignored(void) {
  FirmwareMarkerScanner sc;
  scan(sc, std::string(kFirmwareMarkerPrefix) + "board=x|v=" + std::string(200, 'a') +
               std::string(1, '\0'));
  TEST_ASSERT_FALSE(sc.found());
}

static void test_first_marker_wins(void) {
  FirmwareMarkerScanner sc;
  scan(sc, std::string(kMarker) + std::string(1, '\0') + "CYDSKY-FW1|board=x|v=v9" +
               std::string(1, '\0'));
  TEST_ASSERT_EQUAL_STRING("v1.3.0", sc.version().c_str());
}

// ---- progress

static void test_progress_percent(void) {
  TEST_ASSERT_EQUAL_INT(0, otaProgressPercent(0, 1000));
  TEST_ASSERT_EQUAL_INT(50, otaProgressPercent(500, 1000));
  TEST_ASSERT_EQUAL_INT(100, otaProgressPercent(1000, 1000));
  TEST_ASSERT_EQUAL_INT(100, otaProgressPercent(1200, 1000));
  TEST_ASSERT_EQUAL_INT(-1, otaProgressPercent(10, 0));
  TEST_ASSERT_EQUAL_INT(99, otaProgressPercent(kSlot - 1, kSlot));
}

// ---- messages

static void test_every_result_has_a_message_and_status(void) {
  for (int i = 0; i < kOtaResultCount; ++i) {
    const OtaResult r = static_cast<OtaResult>(i);
    const int status = otaResultHttpStatus(r);
    TEST_ASSERT_TRUE(status >= 200 && status < 600);
    TEST_ASSERT_TRUE(static_cast<int>(otaResultMessage(r)) < kWebStrCount);
  }
  TEST_ASSERT_EQUAL_INT(200, otaResultHttpStatus(OtaResult::Ok));
  TEST_ASSERT_EQUAL_INT(413, otaResultHttpStatus(OtaResult::TooLarge));
  TEST_ASSERT_EQUAL_INT(401, otaResultHttpStatus(OtaResult::Unauthorized));
  TEST_ASSERT_TRUE(otaResultMessage(OtaResult::LooksLikeFull) == WebStr::OtaFullImage);
}

// ---- OtaSession

static void test_good_image_is_written_and_activated(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024);
  TEST_ASSERT_TRUE(upload(s, img, 1436, img.size()) == OtaResult::Ok);
  TEST_ASSERT_EQUAL_INT(1, w.begins);
  TEST_ASSERT_EQUAL_UINT32(img.size(), w.beginSize);
  TEST_ASSERT_EQUAL_INT(1, w.ends);
  TEST_ASSERT_EQUAL_INT(0, w.aborts);
  TEST_ASSERT_TRUE(w.data == img);
  TEST_ASSERT_EQUAL_STRING("v1.3.0", s.marker().version().c_str());
}

static void test_tiny_first_chunks_are_held_until_the_header_is_complete(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024);
  TEST_ASSERT_TRUE(upload(s, img, 7, 0) == OtaResult::Ok);
  TEST_ASSERT_EQUAL_UINT32(0, w.beginSize);  // size unknown
  TEST_ASSERT_TRUE(w.data == img);
}

static void test_full_image_never_reaches_the_writer(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  std::vector<uint8_t> img(700 * 1024, 0xFF);
  TEST_ASSERT_TRUE(upload(s, img, 4096, img.size()) == OtaResult::LooksLikeFull);
  TEST_ASSERT_EQUAL_INT(0, w.begins);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
}

static void test_declared_size_is_checked_before_anything_is_written(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024);
  TEST_ASSERT_TRUE(upload(s, img, 4096, kSlot + 1) == OtaResult::TooLarge);
  TEST_ASSERT_TRUE(upload(s, img, 4096, 17 * 1024) == OtaResult::TooSmall);
  TEST_ASSERT_EQUAL_INT(0, w.begins);
}

static void test_bootloader_sized_file_is_rejected_without_activation(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(17 * 1024, true, 1000);
  TEST_ASSERT_TRUE(upload(s, img, 4096, 0) == OtaResult::TooSmall);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
  TEST_ASSERT_EQUAL_INT(1, w.aborts);
}

static void test_foreign_firmware_is_aborted_at_the_end(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024, false);
  TEST_ASSERT_TRUE(upload(s, img, 4096, img.size()) == OtaResult::NotSkyTracker);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
  TEST_ASSERT_EQUAL_INT(1, w.aborts);
}

static void test_write_error_aborts_and_ignores_the_rest(void) {
  FakeWriter w;
  w.failWriteAfter = 100000;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024);
  TEST_ASSERT_TRUE(upload(s, img, 4096, img.size()) == OtaResult::WriteFailed);
  TEST_ASSERT_EQUAL_INT(1, w.aborts);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
}

static void test_failed_verify_is_reported(void) {
  FakeWriter w;
  w.endOk = false;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024);
  TEST_ASSERT_TRUE(upload(s, img, 4096, img.size()) == OtaResult::VerifyFailed);
  TEST_ASSERT_EQUAL_INT(0, w.aborts);
}

static void test_broken_upload_aborts(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  const std::vector<uint8_t> img = fakeImage(600 * 1024);
  s.start(img.size());
  s.feed(img.data(), 50000);
  s.abort();
  TEST_ASSERT_TRUE(s.result() == OtaResult::Aborted);
  TEST_ASSERT_EQUAL_INT(1, w.aborts);
  TEST_ASSERT_TRUE(s.finish() == OtaResult::Aborted);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
}

static void test_empty_upload_is_no_file(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  s.start(0);
  TEST_ASSERT_TRUE(s.finish() == OtaResult::NoFile);
  TEST_ASSERT_EQUAL_INT(0, w.begins);
}

// ---- downloads from GitHub: SHA-256 and version (release_check)

namespace {
// Not SHA-256 — a stand-in that depends on every byte, which is all
// OtaSession needs to know about its digest.
struct FakeDigest : OtaDigest {
  uint64_t sum = 0;
  size_t bytes = 0;
  int resets = 0;
  void reset() override {
    ++resets;
    sum = 0;
    bytes = 0;
  }
  void update(const uint8_t *d, size_t len) override {
    for (size_t i = 0; i < len; ++i) sum = sum * 31 + d[i];
    bytes += len;
  }
  std::string finishHex() override {
    char buf[65];
    std::snprintf(buf, sizeof(buf), "%064llx", static_cast<unsigned long long>(sum));
    return buf;
  }
};

std::string digestOf(const std::vector<uint8_t> &img) {
  FakeDigest d;
  d.reset();
  d.update(img.data(), img.size());
  return d.finishHex();
}

OtaResult uploadExpecting(OtaSession &s, const std::vector<uint8_t> &img,
                          const OtaExpectations &e) {
  s.start(img.size(), e);
  for (size_t off = 0; off < img.size(); off += 4096) {
    s.feed(img.data() + off, std::min<size_t>(4096, img.size() - off));
  }
  return s.finish();
}
}  // namespace

static void test_matching_checksum_and_version_activate(void) {
  FakeWriter w;
  FakeDigest d;
  OtaSession s(w, kSlot, &d);
  const std::vector<uint8_t> img = fakeImage(700 * 1024);
  OtaExpectations e;
  e.sha256 = digestOf(img);
  e.version = "v1.3.0";  // what kMarker says
  TEST_ASSERT_TRUE(uploadExpecting(s, img, e) == OtaResult::Ok);
  TEST_ASSERT_EQUAL_INT(1, w.ends);
  TEST_ASSERT_EQUAL_UINT32(img.size(), d.bytes);  // every byte, header included
}

static void test_checksum_mismatch_never_activates(void) {
  FakeWriter w;
  FakeDigest d;
  OtaSession s(w, kSlot, &d);
  std::vector<uint8_t> img = fakeImage(700 * 1024);
  OtaExpectations e;
  e.sha256 = digestOf(img);
  img[100000] ^= 0x01;  // one bit flipped on the way
  TEST_ASSERT_TRUE(uploadExpecting(s, img, e) == OtaResult::ChecksumMismatch);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
  TEST_ASSERT_EQUAL_INT(1, w.aborts);
  TEST_ASSERT_EQUAL_INT(400, otaResultHttpStatus(OtaResult::ChecksumMismatch));
}

static void test_expected_checksum_without_a_digest_fails_safe(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);  // no digest to compute it with
  const std::vector<uint8_t> img = fakeImage(700 * 1024);
  OtaExpectations e;
  e.sha256 = std::string(64, 'a');
  TEST_ASSERT_TRUE(uploadExpecting(s, img, e) == OtaResult::ChecksumMismatch);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
}

static void test_version_mismatch_never_activates(void) {
  FakeWriter w;
  FakeDigest d;
  OtaSession s(w, kSlot, &d);
  const std::vector<uint8_t> img = fakeImage(700 * 1024);
  OtaExpectations e;
  e.sha256 = digestOf(img);
  e.version = "v1.4.0";  // the file says v1.3.0
  TEST_ASSERT_TRUE(uploadExpecting(s, img, e) == OtaResult::VersionMismatch);
  TEST_ASSERT_EQUAL_INT(0, w.ends);
}

static void test_marker_is_checked_before_the_checksum(void) {
  FakeWriter w;
  FakeDigest d;
  OtaSession s(w, kSlot, &d);
  const std::vector<uint8_t> img = fakeImage(700 * 1024, /*withMarker=*/false);
  OtaExpectations e;
  e.sha256 = std::string(64, '0');
  TEST_ASSERT_TRUE(uploadExpecting(s, img, e) == OtaResult::NotSkyTracker);
}

static void test_expectations_do_not_leak_into_the_next_upload(void) {
  FakeWriter w;
  FakeDigest d;
  OtaSession s(w, kSlot, &d);
  const std::vector<uint8_t> img = fakeImage(700 * 1024);
  OtaExpectations e;
  e.sha256 = std::string(64, 'f');
  TEST_ASSERT_TRUE(uploadExpecting(s, img, e) == OtaResult::ChecksumMismatch);
  // A file picked by hand next: no expectations, accepted.
  TEST_ASSERT_TRUE(upload(s, img, 4096, img.size()) == OtaResult::Ok);
  TEST_ASSERT_EQUAL_INT(2, d.resets);
}

static void test_in_progress(void) {
  FakeWriter w;
  OtaSession s(w, kSlot);
  TEST_ASSERT_FALSE(s.inProgress());
  const std::vector<uint8_t> img = fakeImage(700 * 1024);
  s.start(img.size());
  TEST_ASSERT_TRUE(s.inProgress());
  s.feed(img.data(), img.size());
  s.finish();
  TEST_ASSERT_FALSE(s.inProgress());
}

static void test_download_failure_is_a_gateway_error(void) {
  TEST_ASSERT_EQUAL_INT(502, otaResultHttpStatus(OtaResult::DownloadFailed));
}

// ---- rollback

static void test_mark_valid_only_when_pending_reachable_and_up_long_enough(void) {
  TEST_ASSERT_FALSE(shouldMarkAppValid(true, 29999, true, true));
  TEST_ASSERT_TRUE(shouldMarkAppValid(true, 30000, true, true));
  TEST_ASSERT_FALSE(shouldMarkAppValid(true, 60000, false, true));
  TEST_ASSERT_FALSE(shouldMarkAppValid(false, 60000, true, true));
  // The network works but the display loop hangs: not valid -> rollback.
  TEST_ASSERT_FALSE(shouldMarkAppValid(true, 60000, true, false));
  // ...and is restarted into the previous image.
  TEST_ASSERT_TRUE(shouldRestartForRollback(true, 30000, false));
  TEST_ASSERT_FALSE(shouldRestartForRollback(true, 29999, false));  // a fair start first
  TEST_ASSERT_FALSE(shouldRestartForRollback(true, 60000, true));   // UI fine
  TEST_ASSERT_FALSE(shouldRestartForRollback(false, 60000, false)); // already valid
}

}  // namespace

void run_ota_update_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp
  RUN_TEST(test_header_ok_for_esp32_app);
  RUN_TEST(test_header_recognises_the_full_image);
  RUN_TEST(test_header_rejects_other_chips_and_junk);
  RUN_TEST(test_size_checks);
  RUN_TEST(test_marker_found_in_one_piece);
  RUN_TEST(test_marker_found_when_split_at_every_point);
  RUN_TEST(test_bare_prefix_is_skipped);
  RUN_TEST(test_marker_needs_terminator_and_fields);
  RUN_TEST(test_overlong_body_is_ignored);
  RUN_TEST(test_first_marker_wins);
  RUN_TEST(test_progress_percent);
  RUN_TEST(test_every_result_has_a_message_and_status);
  RUN_TEST(test_good_image_is_written_and_activated);
  RUN_TEST(test_tiny_first_chunks_are_held_until_the_header_is_complete);
  RUN_TEST(test_full_image_never_reaches_the_writer);
  RUN_TEST(test_declared_size_is_checked_before_anything_is_written);
  RUN_TEST(test_bootloader_sized_file_is_rejected_without_activation);
  RUN_TEST(test_foreign_firmware_is_aborted_at_the_end);
  RUN_TEST(test_write_error_aborts_and_ignores_the_rest);
  RUN_TEST(test_failed_verify_is_reported);
  RUN_TEST(test_broken_upload_aborts);
  RUN_TEST(test_empty_upload_is_no_file);
  RUN_TEST(test_mark_valid_only_when_pending_reachable_and_up_long_enough);
  RUN_TEST(test_matching_checksum_and_version_activate);
  RUN_TEST(test_checksum_mismatch_never_activates);
  RUN_TEST(test_expected_checksum_without_a_digest_fails_safe);
  RUN_TEST(test_version_mismatch_never_activates);
  RUN_TEST(test_marker_is_checked_before_the_checksum);
  RUN_TEST(test_expectations_do_not_leak_into_the_next_upload);
  RUN_TEST(test_in_progress);
  RUN_TEST(test_download_failure_is_a_gateway_error);
}
