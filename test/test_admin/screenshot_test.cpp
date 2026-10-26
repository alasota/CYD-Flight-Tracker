// screenshot: BMP header + band order of /screenshot.bmp, ScreenshotBridge (spec 09 §1). CLAUDE.md "Code conventions" (screenshot).
#include <unity.h>

#include "screenshot.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "test_support.h"

namespace {

static uint32_t le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static uint16_t le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// ---- row stride (BMP rows are padded to 4 bytes) ---------------------------

static void test_row_stride_320_needs_no_padding(void) {
  TEST_ASSERT_EQUAL_UINT32(960, bmpRowStride(320));  // 320 * 3 = 960, already /4
}

static void test_row_stride_pads_to_multiple_of_4(void) {
  TEST_ASSERT_EQUAL_UINT32(4, bmpRowStride(1));    // 3 -> 4
  TEST_ASSERT_EQUAL_UINT32(8, bmpRowStride(2));    // 6 -> 8
  TEST_ASSERT_EQUAL_UINT32(12, bmpRowStride(3));   // 9 -> 12
  TEST_ASSERT_EQUAL_UINT32(12, bmpRowStride(4));   // 12 -> 12
}

// ---- file size -----------------------------------------------------------------

static void test_file_size_for_the_cyd_screen(void) {
  // 54-byte header + 960 bytes x 240 rows.
  TEST_ASSERT_EQUAL_UINT32(54 + 960 * 240, bmpFileSize(320, 240));
}

// ---- header bytes ---------------------------------------------------------------

static void test_header_fields(void) {
  uint8_t h[kBmpHeaderSize];
  writeBmpHeader(h, 320, 240);

  TEST_ASSERT_EQUAL_UINT8('B', h[0]);
  TEST_ASSERT_EQUAL_UINT8('M', h[1]);
  TEST_ASSERT_EQUAL_UINT32(bmpFileSize(320, 240), le32(&h[2]));
  TEST_ASSERT_EQUAL_UINT32(0, le32(&h[6]));    // reserved
  TEST_ASSERT_EQUAL_UINT32(54, le32(&h[10]));  // pixel data offset

  TEST_ASSERT_EQUAL_UINT32(40, le32(&h[14]));   // BITMAPINFOHEADER size
  TEST_ASSERT_EQUAL_UINT32(320, le32(&h[18]));  // width
  TEST_ASSERT_EQUAL_UINT32(240, le32(&h[22]));  // height > 0 = bottom-up rows
  TEST_ASSERT_EQUAL_UINT16(1, le16(&h[26]));    // planes
  TEST_ASSERT_EQUAL_UINT16(24, le16(&h[28]));   // bits per pixel
  TEST_ASSERT_EQUAL_UINT32(0, le32(&h[30]));    // BI_RGB, uncompressed
  TEST_ASSERT_EQUAL_UINT32(960 * 240, le32(&h[34]));  // image size
}

static void test_header_size_constant(void) {
  TEST_ASSERT_EQUAL_INT(54, kBmpHeaderSize);
}

// ---- screen selector from ?screen= ------------------------------------------------

static void test_parse_screen_arg(void) {
  TEST_ASSERT_EQUAL_INT(0, parseScreenshotScreenArg("0", 3));
  TEST_ASSERT_EQUAL_INT(2, parseScreenshotScreenArg("2", 3));
  TEST_ASSERT_EQUAL_INT(-1, parseScreenshotScreenArg("", 3));    // absent -> keep current
  TEST_ASSERT_EQUAL_INT(-1, parseScreenshotScreenArg("3", 3));   // out of range
  TEST_ASSERT_EQUAL_INT(-1, parseScreenshotScreenArg("-1", 3));
  TEST_ASSERT_EQUAL_INT(-1, parseScreenshotScreenArg("x", 3));
  TEST_ASSERT_EQUAL_INT(-1, parseScreenshotScreenArg("1a", 3));
}

// ---- bands, bottom-up ---------------------------------------------------------

static void test_band_count(void) {
  TEST_ASSERT_EQUAL_INT(15, screenshotBandCount(240, 16));
  TEST_ASSERT_EQUAL_INT(30, screenshotBandCount(240, 8));
  TEST_ASSERT_EQUAL_INT(4, screenshotBandCount(50, 16));  // 16+16+16+2
  TEST_ASSERT_EQUAL_INT(0, screenshotBandCount(0, 16));
  TEST_ASSERT_EQUAL_INT(0, screenshotBandCount(240, 0));
}

static void test_bands_start_at_the_bottom(void) {
  ScreenshotBand b = screenshotBand(240, 16, 0);
  TEST_ASSERT_EQUAL_INT(224, b.y0);
  TEST_ASSERT_EQUAL_INT(16, b.rows);
  b = screenshotBand(240, 16, 14);
  TEST_ASSERT_EQUAL_INT(0, b.y0);
  TEST_ASSERT_EQUAL_INT(16, b.rows);
}

static void test_short_last_band_and_full_cover(void) {
  ScreenshotBand last = screenshotBand(50, 16, 3);
  TEST_ASSERT_EQUAL_INT(0, last.y0);
  TEST_ASSERT_EQUAL_INT(2, last.rows);
  // Every row exactly once, contiguous, from the bottom up.
  int expectedEnd = 50;
  int total = 0;
  for (int i = 0; i < screenshotBandCount(50, 16); ++i) {
    const ScreenshotBand b = screenshotBand(50, 16, i);
    TEST_ASSERT_EQUAL_INT(expectedEnd, b.y0 + b.rows);
    expectedEnd = b.y0;
    total += b.rows;
  }
  TEST_ASSERT_EQUAL_INT(0, expectedEnd);
  TEST_ASSERT_EQUAL_INT(50, total);
  TEST_ASSERT_EQUAL_INT(0, screenshotBand(50, 16, 4).rows);  // out of range
}

// ---- ScreenshotBridge: handler <-> UI hand-over (spec 09 §1) -------------------
//
// Single-threaded tests: the handler's wait step (`pause`) runs `onPause` —
// by default one UI pass, standing in for core 1; tests that need the UI at
// a particular moment step it by hand. A fake panel: every byte of row y is
// (y + 1), never the poison 0xA5.

constexpr int kShotW = 4;  // px
constexpr int kShotH = 16;  // rows
constexpr int kShotBandRows = 4;
constexpr size_t kShotRowBytes = 3 * kShotW;  // 12, already a multiple of 4
constexpr uint8_t kPoison = 0xA5;

struct NoLock {
  void lock() {}
  void unlock() {}
};
using Bridge = ScreenshotBridge<NoLock>;

static uint8_t panelByte(int y) { return static_cast<uint8_t>(y + 1); }
static uint32_t shotStride() { return bmpRowStride(kShotW); }

struct ShotRig {
  uint32_t now = 0;
  std::function<void()> onPause;  // default: one UI pass
  int prepared = 0;
  int preparedScreen = -2;
  Bridge bridge;

  ShotRig()
      : bridge(Bridge::Hooks{[this] { return now; }, [] {},
                             [this] {
                               ++now;
                               if (onPause) onPause();
                             }}) {
    onPause = [this] { uiPass(); };
  }
  ShotRig(const ShotRig &) = delete;
  ShotRig &operator=(const ShotRig &) = delete;

  // Like main.cpp's serviceScreenshot(): answer whatever was asked, then
  // report whether drawing is held.
  bool uiPass() {
    const Bridge::Job job = bridge.takeJob();
    if (job.kind == Bridge::Job::Prepare) {
      ++prepared;
      preparedScreen = job.screen;
    } else if (job.kind == Bridge::Job::Band) {
      readBand(job, 0, job.rows);
    }
    if (job.kind != Bridge::Job::None) bridge.finishJob(job, now);
    return bridge.capturing(now);
  }

  // Rows [from, to) of the job's band, bottom row first; false = abandoned.
  bool readBand(const Bridge::Job &job, int from, int to) {
    for (int i = from; i < to; ++i) {
      uint8_t row[kShotRowBytes];
      std::memset(row, panelByte(job.y0 + job.rows - 1 - i), sizeof(row));
      if (!bridge.writeRow(job, i, row, sizeof(row))) return false;
    }
    return true;
  }
};

static std::vector<uint8_t> poisonedBuffer() {
  return std::vector<uint8_t>(shotStride() * kShotBandRows, kPoison);
}

static void assertAllPoison(const std::vector<uint8_t> &buf, const char *what) {
  for (size_t i = 0; i < buf.size(); ++i) {
    if (buf[i] != kPoison) {
      TEST_FAIL_MESSAGE((std::string(what) + ": byte " + std::to_string(i) + " was written").c_str());
    }
  }
}

// Row i of a band buffer holds panel row y0 + rows - 1 - i (BMP, bottom-up).
static void assertBandRows(const std::vector<uint8_t> &buf, ScreenshotBand band) {
  for (int i = 0; i < band.rows; ++i) {
    const uint8_t want = panelByte(band.y0 + band.rows - 1 - i);
    for (size_t x = 0; x < kShotRowBytes; ++x) {
      TEST_ASSERT_EQUAL_UINT8(want, buf[static_cast<size_t>(i) * shotStride() + x]);
    }
  }
}

// A capture of one band at the bottom, answered by the UI; leaves the bridge
// in BandDone (the handler would now send it).
static void captureOneBand(ShotRig &rig, std::vector<uint8_t> &buf) {
  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));
  const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, 0);
  rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
  TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
  assertBandRows(buf, band);
}

// Zwykły zrzut — wszystkie pasy trafiają do buforów, potem ekran rysuje dalej
static void test_bridge_normal_capture_fills_every_band(void) {
  ShotRig rig;
  TEST_ASSERT_TRUE(rig.bridge.begin(1, 2000));
  TEST_ASSERT_EQUAL_INT(1, rig.prepared);
  TEST_ASSERT_EQUAL_INT(1, rig.preparedScreen);

  std::vector<uint8_t> bufs[2] = {poisonedBuffer(), poisonedBuffer()};
  const int bands = screenshotBandCount(kShotH, kShotBandRows);
  for (int k = 0; k < bands; ++k) {
    const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, k);
    std::vector<uint8_t> &buf = bufs[k % 2];
    rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
    TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
    assertBandRows(buf, band);
    TEST_ASSERT_TRUE(rig.bridge.capturing(rig.now));  // no drawing during the capture
  }
  rig.bridge.end();
  TEST_ASSERT_FALSE(rig.uiPass());  // draws again at once
  ASSERT_ENUM_EQ(Bridge::Idle, rig.bridge.stateForTesting());
}

// Timeout w trakcie pasa nie zostawia „BandDone” i nie blokuje rysowania
static void test_bridge_timeout_mid_band_leaves_no_band_done(void) {
  ShotRig rig;
  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));
  rig.onPause = nullptr;  // from here on the test steps the UI

  std::vector<uint8_t> buf = poisonedBuffer();
  const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, 0);
  rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
  const Bridge::Job job = rig.bridge.takeJob();
  ASSERT_ENUM_EQ(Bridge::Job::Band, job.kind);
  TEST_ASSERT_TRUE(rig.readBand(job, 0, 2));  // the UI is reading the band off the panel

  TEST_ASSERT_FALSE(rig.bridge.waitBand(3));           // the handler gives up
  TEST_ASSERT_FALSE(rig.readBand(job, 2, band.rows));  // the rest is dropped
  rig.bridge.finishJob(job, rig.now);                  // the UI ends the band late

  ASSERT_ENUM_EQ(Bridge::Idle, rig.bridge.stateForTesting());  // no BandDone left behind
  TEST_ASSERT_FALSE(rig.uiPass());                               // draws again, no 5 s wait
  for (size_t x = 0; x < kShotRowBytes; ++x) {
    TEST_ASSERT_EQUAL_UINT8(kPoison, buf[2 * shotStride() + x]);
    TEST_ASSERT_EQUAL_UINT8(kPoison, buf[3 * shotStride() + x]);
  }
}

// Spóźnione przygotowanie ekranu po timeoucie begin nie wznawia zrzutu
static void test_bridge_late_prepare_after_begin_timeout_is_ignored(void) {
  ShotRig rig;
  Bridge::Job job;
  bool taken = false;
  // The UI picks the request up on the first wait step but is slow to finish it.
  rig.onPause = [&] {
    if (!taken) {
      job = rig.bridge.takeJob();
      taken = true;
    }
  };
  TEST_ASSERT_FALSE(rig.bridge.begin(0, 3));
  ASSERT_ENUM_EQ(Bridge::Job::Prepare, job.kind);

  rig.bridge.finishJob(job, rig.now);  // screen prepared, too late
  ASSERT_ENUM_EQ(Bridge::Idle, rig.bridge.stateForTesting());
  TEST_ASSERT_FALSE(rig.bridge.capturing(rig.now));
  TEST_ASSERT_FALSE(rig.uiPass());
}

// Pas ze starego żądania nie pisze do bufora nowego żądania
static void test_bridge_stale_band_does_not_write_new_buffer(void) {
  ShotRig rig;
  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));  // request A

  // A asks for the bottom band; the UI takes it and is still reading when
  // A times out, ends and frees its buffer.
  std::vector<uint8_t> bufA = poisonedBuffer();
  const ScreenshotBand bandA = screenshotBand(kShotH, kShotBandRows, 0);  // rows 12..15
  Bridge::Job jobA;
  rig.onPause = [&] {
    if (jobA.kind == Bridge::Job::None) jobA = rig.bridge.takeJob();
  };
  rig.bridge.requestBand(bandA.y0, bandA.rows, shotStride(), bufA.data());
  TEST_ASSERT_FALSE(rig.bridge.waitBand(3));
  ASSERT_ENUM_EQ(Bridge::Job::Band, jobA.kind);
  rig.bridge.end();
  std::fill(bufA.begin(), bufA.end(), kPoison);  // "freed"

  // Request B (new capture, new buffer) arrives before the UI is done with
  // A's band: the UI's first step finishes the old band, later steps serve B.
  int step = 0;
  bool oldBandWritten = true;
  Bridge::State stateAfterOldBand = Bridge::BandDone;
  rig.onPause = [&] {
    if (step++ == 0) {
      oldBandWritten = rig.readBand(jobA, 0, jobA.rows);
      rig.bridge.finishJob(jobA, rig.now);
      stateAfterOldBand = rig.bridge.stateForTesting();
    } else {
      rig.uiPass();
    }
  };
  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));
  TEST_ASSERT_FALSE(oldBandWritten);                     // the UI abandoned the old band
  ASSERT_ENUM_NE(Bridge::BandDone, stateAfterOldBand);  // the old band isn't "done" for B
  ASSERT_ENUM_NE(Bridge::BeginDone, stateAfterOldBand);

  std::vector<uint8_t> bufB = poisonedBuffer();
  const ScreenshotBand bandB = screenshotBand(kShotH, kShotBandRows, 2);  // rows 4..7
  rig.bridge.requestBand(bandB.y0, bandB.rows, shotStride(), bufB.data());
  ASSERT_ENUM_EQ(Bridge::BandAsked, rig.bridge.stateForTesting());
  TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
  assertBandRows(bufB, bandB);  // B's rows only, none of A's (rows 12..15)
  assertAllPoison(bufA, "freed buffer of request A");
  rig.bridge.end();
}

// Po end() spóźniony pas niczego już nie zapisuje
static void test_bridge_end_invalidates_outstanding_band(void) {
  ShotRig rig;
  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));
  rig.onPause = nullptr;

  std::vector<uint8_t> buf = poisonedBuffer();
  const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, 0);
  rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
  const Bridge::Job job = rig.bridge.takeJob();
  ASSERT_ENUM_EQ(Bridge::Job::Band, job.kind);

  rig.bridge.end();
  std::fill(buf.begin(), buf.end(), kPoison);  // "freed"

  uint8_t row[kShotRowBytes];
  std::memset(row, panelByte(band.y0 + band.rows - 1), sizeof(row));
  TEST_ASSERT_FALSE(rig.bridge.writeRow(job, 0, row, sizeof(row)));  // abandon the band
  rig.bridge.finishJob(job, rig.now);
  assertAllPoison(buf, "buffer after end()");
  ASSERT_ENUM_EQ(Bridge::Idle, rig.bridge.stateForTesting());
  TEST_ASSERT_FALSE(rig.bridge.capturing(rig.now));
}

// Klient zniknął bez end() — UI wraca do rysowania po 5 s bez aktywności
// (Examples: 4999 -> nie, 5000 -> nie, 5001 -> znowu)
static void test_bridge_idle_client_resumes_drawing_after_5_s(void) {
  const struct {
    uint32_t ms;
    bool drawing;
  } examples[] = {{4999, false}, {5000, false}, {5001, true}};
  for (const auto &ex : examples) {
    ShotRig rig;
    std::vector<uint8_t> buf = poisonedBuffer();
    captureOneBand(rig, buf);
    const uint32_t t = rig.now;  // the UI handed the band over at T
    rig.onPause = nullptr;
    rig.now = t + ex.ms;
    TEST_ASSERT_EQUAL_MESSAGE(!ex.drawing, rig.uiPass(), std::to_string(ex.ms).c_str());
  }
}

// ---- concurrent: two threads, one bridge ---------------------------------------

struct MutexLock {
  std::mutex m;
  void lock() { m.lock(); }
  void unlock() { m.unlock(); }
};
using ThreadBridge = ScreenshotBridge<MutexLock>;

struct ConcurrentRun {
  int completed = 0;
  int aborted = 0;
  int badBands = 0;       // a completed band whose rows were wrong
  int poisonBroken = 0;   // retired buffers written after retirement
  bool capturingAfter = true;
};

// `captures` captures with random timeouts / early ends. Retired buffers are
// poisoned, then either kept (and checked at the end) or really freed (so
// AddressSanitizer in CI sees a late write as a use-after-free).
static ConcurrentRun runConcurrentCaptures(int captures, bool reallyFree, uint32_t seed) {
  constexpr uint32_t kNoTimeout = 50000000;  // fake ms; a correct UI answers long before
  const size_t bufBytes = static_cast<size_t>(shotStride()) * kShotBandRows;
  std::atomic<uint32_t> clock{0};
  std::atomic<bool> stop{false};
  ThreadBridge bridge(ThreadBridge::Hooks{[&clock] { return clock.load(); }, [] {},
                                          [&clock] {
                                            clock.fetch_add(1);
                                            std::this_thread::yield();
                                          }});

  std::thread ui([&] {
    std::mt19937 rng(seed ^ 0x5EEDu);
    while (!stop.load()) {
      const ThreadBridge::Job job = bridge.takeJob();
      if (job.kind == ThreadBridge::Job::Band) {
        for (int i = 0; i < job.rows; ++i) {
          if (rng() % 4 == 0) std::this_thread::yield();  // slow panel reads
          uint8_t row[kShotRowBytes];
          std::memset(row, panelByte(job.y0 + job.rows - 1 - i), sizeof(row));
          if (!bridge.writeRow(job, i, row, sizeof(row))) break;
        }
      }
      if (job.kind != ThreadBridge::Job::None) {
        if (rng() % 4 == 0) std::this_thread::yield();
        bridge.finishJob(job, clock.load());
      } else {
        std::this_thread::yield();
      }
      bridge.capturing(clock.load());
    }
  });

  ConcurrentRun r;
  std::mt19937 rng(seed);
  auto timeout = [&rng]() -> uint32_t {
    switch (rng() % 8) {
      case 0: return 0;
      case 1: return 1;
      case 2: return rng() % 8;
      default: return kNoTimeout;
    }
  };
  std::vector<uint8_t *> graveyard;
  const int bands = screenshotBandCount(kShotH, kShotBandRows);
  for (int c = 0; c < captures; ++c) {
    uint8_t *bufs[2] = {new uint8_t[bufBytes](), new uint8_t[bufBytes]()};
    bool aborted = !bridge.begin(-1, timeout());
    for (int k = 0; k < bands && !aborted; ++k) {
      const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, k);
      uint8_t *buf = bufs[k % 2];
      if (rng() % 32 == 0) { aborted = true; break; }  // client gone between bands
      bridge.requestBand(band.y0, band.rows, shotStride(), buf);
      if (rng() % 32 == 0) { aborted = true; break; }  // gone right after asking
      if (!bridge.waitBand(timeout())) { aborted = true; break; }
      for (int i = 0; i < band.rows; ++i) {
        const uint8_t want = panelByte(band.y0 + band.rows - 1 - i);
        for (size_t x = 0; x < kShotRowBytes; ++x) {
          if (buf[static_cast<size_t>(i) * shotStride() + x] != want) {
            ++r.badBands;
            i = band.rows;
            break;
          }
        }
      }
    }
    bridge.end();
    if (aborted) ++r.aborted;
    else ++r.completed;
    for (uint8_t *b : bufs) {
      std::memset(b, kPoison, bufBytes);
      if (reallyFree) delete[] b;
      else graveyard.push_back(b);
    }
  }
  stop.store(true);
  ui.join();
  r.capturingAfter = bridge.capturing(clock.load());
  for (uint8_t *b : graveyard) {
    for (size_t i = 0; i < bufBytes; ++i) {
      if (b[i] != kPoison) {
        ++r.poisonBroken;
        break;
      }
    }
    delete[] b;
  }
  return r;
}

// Współbieżnie — 10 000 zrzutów z losowymi timeoutami nie pisze do zwolnionej pamięci
static void test_bridge_concurrent_random_timeouts_never_touch_freed_buffers(void) {
  const ConcurrentRun kept = runConcurrentCaptures(10000, /*reallyFree=*/false, 9u);
  TEST_ASSERT_EQUAL_INT(0, kept.poisonBroken);  // no write after a buffer was retired
  TEST_ASSERT_TRUE(kept.completed > 0);          // the run did capture something
  TEST_ASSERT_TRUE(kept.aborted > 0);            // ... and did interrupt something
  TEST_ASSERT_EQUAL_INT(0, kept.badBands);       // every finished capture is complete
  TEST_ASSERT_FALSE(kept.capturingAfter);        // the UI draws again afterwards

  // The same with real delete[] — a late write is a use-after-free for ASan (CI).
  const ConcurrentRun freed = runConcurrentCaptures(2000, /*reallyFree=*/true, 10u);
  TEST_ASSERT_TRUE(freed.completed > 0);
  TEST_ASSERT_EQUAL_INT(0, freed.badBands);
  TEST_ASSERT_FALSE(freed.capturingAfter);
}

// ---- bridge edge cases (plan, no scenario) ---------------------------------------

// The generation counter wraps past UINT32_MAX without ever handing out 0
// ("nothing valid"), and captures keep working across the wrap.
static void test_bridge_generation_wrap_skips_zero(void) {
  ShotRig rig;
  rig.bridge.setGenerationForTesting(UINT32_MAX - 2);
  std::vector<uint32_t> gens;
  rig.onPause = [&] {
    const Bridge::Job job = rig.bridge.takeJob();
    if (job.kind != Bridge::Job::None) gens.push_back(job.gen);
    if (job.kind == Bridge::Job::Band) rig.readBand(job, 0, job.rows);
    if (job.kind != Bridge::Job::None) rig.bridge.finishJob(job, rig.now);
  };
  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));
  std::vector<uint8_t> buf = poisonedBuffer();
  for (int k = 0; k < 4; ++k) {
    const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, k);
    std::fill(buf.begin(), buf.end(), kPoison);
    rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
    TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
    assertBandRows(buf, band);
  }
  rig.bridge.end();
  TEST_ASSERT_EQUAL_size_t(5, gens.size());
  for (uint32_t g : gens) TEST_ASSERT_NOT_EQUAL(0u, g);
}

// waitBand() with nothing asked times out cleanly, and the next capture works.
static void test_bridge_wait_band_without_request_times_out_cleanly(void) {
  ShotRig rig;
  TEST_ASSERT_FALSE(rig.bridge.waitBand(3));
  ASSERT_ENUM_EQ(Bridge::Idle, rig.bridge.stateForTesting());
  TEST_ASSERT_FALSE(rig.uiPass());
  std::vector<uint8_t> buf = poisonedBuffer();
  captureOneBand(rig, buf);
}

// takeJob() with nothing asked returns None and leaves `capturing` alone —
// Idle stays "drawing", a capture between bands stays "held".
static void test_bridge_take_job_without_request_changes_nothing(void) {
  ShotRig rig;
  ASSERT_ENUM_EQ(Bridge::Job::None, rig.bridge.takeJob().kind);
  TEST_ASSERT_FALSE(rig.bridge.capturing(rig.now));

  std::vector<uint8_t> buf = poisonedBuffer();
  captureOneBand(rig, buf);  // BandDone, waiting for the next request
  ASSERT_ENUM_EQ(Bridge::Job::None, rig.bridge.takeJob().kind);
  TEST_ASSERT_TRUE(rig.bridge.capturing(rig.now));
  ASSERT_ENUM_EQ(Bridge::BandDone, rig.bridge.stateForTesting());
}

// After the 5 s idle rule released the UI, a new capture starts normally.
static void test_bridge_new_capture_after_idle_release(void) {
  ShotRig rig;
  std::vector<uint8_t> buf = poisonedBuffer();
  captureOneBand(rig, buf);
  rig.now += Bridge::kCaptureIdleMs + 1;
  TEST_ASSERT_FALSE(rig.uiPass());

  TEST_ASSERT_TRUE(rig.bridge.begin(-1, 2000));
  TEST_ASSERT_TRUE(rig.bridge.capturing(rig.now));
  const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, 1);
  std::fill(buf.begin(), buf.end(), kPoison);
  rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
  TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
  assertBandRows(buf, band);
  rig.bridge.end();
}

// The 5 s idle rule across a millis() rollover.
static void test_bridge_idle_rule_across_millis_rollover(void) {
  ShotRig rig;
  rig.now = 0xFFFFF000u;  // ~4 s before the wrap
  std::vector<uint8_t> buf = poisonedBuffer();
  captureOneBand(rig, buf);
  const uint32_t t = rig.now;
  rig.onPause = nullptr;
  rig.now = t + 4999;  // wrapped
  TEST_ASSERT_TRUE(rig.bridge.capturing(rig.now));
  rig.now = t + 5001;
  TEST_ASSERT_FALSE(rig.bridge.capturing(rig.now));
}

}  // namespace

void run_screenshot_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_row_stride_320_needs_no_padding);
  RUN_TEST(test_row_stride_pads_to_multiple_of_4);
  RUN_TEST(test_file_size_for_the_cyd_screen);
  RUN_TEST(test_header_fields);
  RUN_TEST(test_header_size_constant);
  RUN_TEST(test_parse_screen_arg);
  RUN_TEST(test_band_count);
  RUN_TEST(test_bands_start_at_the_bottom);
  RUN_TEST(test_short_last_band_and_full_cover);

  RUN_TEST(test_bridge_normal_capture_fills_every_band);
  RUN_TEST(test_bridge_timeout_mid_band_leaves_no_band_done);
  RUN_TEST(test_bridge_late_prepare_after_begin_timeout_is_ignored);
  RUN_TEST(test_bridge_stale_band_does_not_write_new_buffer);
  RUN_TEST(test_bridge_end_invalidates_outstanding_band);
  RUN_TEST(test_bridge_idle_client_resumes_drawing_after_5_s);
  RUN_TEST(test_bridge_concurrent_random_timeouts_never_touch_freed_buffers);
  RUN_TEST(test_bridge_generation_wrap_skips_zero);
  RUN_TEST(test_bridge_wait_band_without_request_times_out_cleanly);
  RUN_TEST(test_bridge_take_job_without_request_changes_nothing);
  RUN_TEST(test_bridge_new_capture_after_idle_release);
  RUN_TEST(test_bridge_idle_rule_across_millis_rollover);

}
