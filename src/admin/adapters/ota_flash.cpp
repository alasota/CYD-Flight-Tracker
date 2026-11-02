// ota_flash — the device half of ota_update: the one OtaSession that writes the
// spare app slot through arduino-esp32's Update, with mbedTLS SHA-256 (the
// ESP32's accelerator) for the checksum. Hardware adapter, not covered by
// Unity (CLAUDE.md "Testing").
#include "admin/core/firmware/ota_update.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

namespace {

class UpdateWriter : public OtaWriter {
 public:
  bool begin(size_t size) override { return Update.begin(size > 0 ? size : UPDATE_SIZE_UNKNOWN); }
  size_t write(const uint8_t *data, size_t len) override {
    return Update.write(const_cast<uint8_t *>(data), len);
  }
  bool end() override { return Update.end(true); }
  void abort() override { Update.abort(); }
};

// mbedTLS 2.28 (arduino-esp32 2.0.17); the ESP32's SHA accelerator does the work.
class MbedSha256 : public OtaDigest {
 public:
  MbedSha256() { mbedtls_sha256_init(&ctx_); }
  void reset() override { mbedtls_sha256_starts_ret(&ctx_, /*is224=*/0); }
  void update(const uint8_t *data, size_t len) override {
    mbedtls_sha256_update_ret(&ctx_, data, len);
  }
  std::string finishHex() override {
    uint8_t sum[32];
    mbedtls_sha256_finish_ret(&ctx_, sum);
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t b : sum) {
      out += kHex[b >> 4];
      out += kHex[b & 0x0F];
    }
    return out;
  }

 private:
  mbedtls_sha256_context ctx_;
};

}  // namespace

OtaSession &otaFlashSession() {
  static UpdateWriter writer;
  static MbedSha256 digest;
  static const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  static OtaSession session(writer, slot != nullptr ? slot->size : 0, &digest);
  return session;
}

const char *otaFlashError() { return Update.errorString(); }
