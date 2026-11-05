// screenshot_bridge: the handler <-> UI hand-over of /screenshot.bmp (ScreenshotBridge, ScreenshotReturn), what ?screen=N prepares, one UI pass of a capture. spec 09 §1, spec 10 §2.2.
#include <unity.h>

#include "display/services/screenshot_bridge.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "display/core/screen_nav.h"    // kScreenCount
#include "display/services/screenshot.h"  // bmpRowStride(), screenshotBand()
#include "test_support.h"

namespace {

// ?screen=N wybiera ekran albo jeden z dwóch ekranów demo
static void test_screenshot_prepare_target_screens_and_demos(void) {
  struct Example {
    int index;
    ScreenshotPrepareKind kind;
    int screen;
  };
  const Example examples[] = {
      {-1, ScreenshotPrepareKind::Keep, -1},
      {0, ScreenshotPrepareKind::Screen, 0},
      {2, ScreenshotPrepareKind::Screen, 2},
      {3, ScreenshotPrepareKind::UpdatePromptDemo, -1},
      {4, ScreenshotPrepareKind::AlertDemo, -1},
  };
  for (const Example &e : examples) {
    const std::string what = "screen=" + std::to_string(e.index);
    const ScreenshotPrepare p = screenshotPrepareTarget(e.index, kScreenCount);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.kind), static_cast<int>(p.kind),
                                  what.c_str());
    if (e.kind == ScreenshotPrepareKind::Screen) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(e.screen, p.screen, what.c_str());
    }
  }
}

// -- edge case: past the two demos nothing is switched; the demos follow the screen count
static void test_screenshot_prepare_target_out_of_range_keeps(void) {
  ASSERT_ENUM_EQ(ScreenshotPrepareKind::Keep,
                 screenshotPrepareTarget(kScreenCount + 2, kScreenCount).kind);
  ASSERT_ENUM_EQ(ScreenshotPrepareKind::Keep, screenshotPrepareTarget(-7, kScreenCount).kind);
  ASSERT_ENUM_EQ(ScreenshotPrepareKind::Screen, screenshotPrepareTarget(1, 2).kind);
  ASSERT_ENUM_EQ(ScreenshotPrepareKind::UpdatePromptDemo, screenshotPrepareTarget(2, 2).kind);
  ASSERT_ENUM_EQ(ScreenshotPrepareKind::AlertDemo, screenshotPrepareTarget(3, 2).kind);
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

  // Like screenshotUiPass(): answer whatever was asked, then
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
  TEST_ASSERT_GREATER_THAN_INT(0, kept.completed);  // the run did capture something
  TEST_ASSERT_GREATER_THAN_INT(0, kept.aborted);  // ... and did interrupt something
  TEST_ASSERT_EQUAL_INT(0, kept.badBands);       // every finished capture is complete
  TEST_ASSERT_FALSE(kept.capturingAfter);        // the UI draws again afterwards

  // The same with real delete[] — a late write is a use-after-free for ASan (CI).
  const ConcurrentRun freed = runConcurrentCaptures(2000, /*reallyFree=*/true, 10u);
  TEST_ASSERT_GREATER_THAN_INT(0, freed.completed);
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


// ---- ScreenshotReturn: back to the screen shown before the capture (spec 10) ---
//
// A stand-in for the UI loop: a Prepare job remembers the return target and
// switches `shown` to a real screen (0..2; 3 and 4 are demo screens drawn
// over the current one), every pass feeds capturing() into afterPass().

constexpr int kRealScreens = 3;

struct ReturnRig {
  ShotRig rig;
  ScreenshotReturn ret;
  int shown;
  int returns = 0;  // how many times afterPass() named a screen

  explicit ReturnRig(int startScreen) : shown(startScreen) {
    rig.onPause = [this] { uiPass(); };
  }
  ReturnRig(const ReturnRig &) = delete;
  ReturnRig &operator=(const ReturnRig &) = delete;

  bool uiPass() {
    const Bridge::Job job = rig.bridge.takeJob();
    if (job.kind == Bridge::Job::Prepare) {
      ret.onPrepare(shown, job.screen, kRealScreens);
      if (job.screen >= 0 && job.screen < kRealScreens) shown = job.screen;
    } else if (job.kind == Bridge::Job::Band) {
      rig.readBand(job, 0, job.rows);
    }
    if (job.kind != Bridge::Job::None) rig.bridge.finishJob(job, rig.now);
    const bool capturing = rig.bridge.capturing(rig.now);
    const int back = ret.afterPass(capturing);
    if (back >= 0) {
      shown = back;
      ++returns;
    }
    return capturing;
  }

  // begin(screen) + every band, without end().
  void captureAllBands(int screen) {
    TEST_ASSERT_TRUE(rig.bridge.begin(screen, 2000));
    std::vector<uint8_t> buf = poisonedBuffer();
    for (int k = 0; k < screenshotBandCount(kShotH, kShotBandRows); ++k) {
      const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, k);
      rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
      TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
    }
  }
};

// Po zrzucie innego ekranu urządzenie wraca na ekran sprzed zrzutu
static void test_screen_returns_to_previous_after_capture(void) {
  ReturnRig r(0);
  r.captureAllBands(2);
  TEST_ASSERT_EQUAL_INT(2, r.shown);    // the capture shows screen 2
  TEST_ASSERT_EQUAL_INT(0, r.returns);  // no return while capturing
  r.rig.bridge.end();
  TEST_ASSERT_FALSE(r.uiPass());
  TEST_ASSERT_EQUAL_INT(0, r.shown);  // back on LOTY
  TEST_ASSERT_EQUAL_INT(1, r.returns);
  r.uiPass();
  r.uiPass();
  TEST_ASSERT_EQUAL_INT(1, r.returns);  // exactly once
  TEST_ASSERT_EQUAL_INT(0, r.shown);
}

// Powrót następuje także, gdy klient zniknął w trakcie zrzutu
static void test_screen_returns_after_abandoned_capture(void) {
  ReturnRig r(2);
  TEST_ASSERT_TRUE(r.rig.bridge.begin(1, 2000));
  std::vector<uint8_t> buf = poisonedBuffer();
  const ScreenshotBand band = screenshotBand(kShotH, kShotBandRows, 0);
  r.rig.bridge.requestBand(band.y0, band.rows, shotStride(), buf.data());
  TEST_ASSERT_TRUE(r.rig.bridge.waitBand(2000));
  TEST_ASSERT_EQUAL_INT(1, r.shown);
  // The client stops asking for bands; no end().
  r.rig.onPause = nullptr;
  const uint32_t t = r.rig.now;
  r.rig.now = t + 5000;
  TEST_ASSERT_TRUE(r.uiPass());  // still held at exactly 5 s
  TEST_ASSERT_EQUAL_INT(0, r.returns);
  r.rig.now = t + Bridge::kCaptureIdleMs + 1;
  TEST_ASSERT_FALSE(r.uiPass());  // draws again
  TEST_ASSERT_EQUAL_INT(2, r.shown);
  TEST_ASSERT_EQUAL_INT(1, r.returns);
}

// Bez przełączenia ekranu nie ma do czego wracać
static void test_no_return_when_capture_did_not_switch_screen(void) {
  const int examples[] = {-1, 1, 3, 4};  // no screen arg, same screen, demo 3, demo 4
  for (int screen : examples) {
    ReturnRig r(1);
    r.captureAllBands(screen);
    r.rig.bridge.end();
    r.uiPass();
    r.uiPass();
    const std::string what = "screen=" + std::to_string(screen);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, r.returns, what.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.shown, what.c_str());
  }
}

// -- edge cases (no scenario)

// screenshots.py asks for several screens in a row; when the next begin()
// arrives before the UI saw the previous end() (no capturing true -> false
// edge in between), the series returns to the screen shown before its first
// capture, not to the series' previous capture screen.
static void test_return_target_is_first_screen_of_capture_series(void) {
  ReturnRig r(0);
  r.captureAllBands(1);
  r.rig.bridge.end();
  r.captureAllBands(2);  // begin() before any UI pass after end()
  TEST_ASSERT_EQUAL_INT(2, r.shown);
  TEST_ASSERT_EQUAL_INT(0, r.returns);
  r.rig.bridge.end();
  r.uiPass();
  TEST_ASSERT_EQUAL_INT(0, r.shown);
  TEST_ASSERT_EQUAL_INT(1, r.returns);
}

// Two separate captures, the screen changed by touch in between: each goes
// back to the screen shown before *it*.
static void test_each_finished_capture_returns_to_its_own_previous_screen(void) {
  ReturnRig r(0);
  r.captureAllBands(1);
  r.rig.bridge.end();
  r.uiPass();
  TEST_ASSERT_EQUAL_INT(0, r.shown);
  r.shown = 2;  // the user taps RADAR
  r.captureAllBands(0);
  TEST_ASSERT_EQUAL_INT(0, r.shown);
  r.rig.bridge.end();
  r.uiPass();
  TEST_ASSERT_EQUAL_INT(2, r.shown);
  TEST_ASSERT_EQUAL_INT(2, r.returns);
}

// Regression: the UI took the Prepare job and switched the screen, but the
// handler's begin() timed out before finishJob() — capturing() never went
// true, so the old edge-triggered afterPass() never fired: the display stayed
// on the captured screen and the stale target jumped back at the end of some
// later, unrelated capture.
static void test_screen_returns_when_prepare_finished_after_begin_timeout(void) {
  ReturnRig r(2);
  Bridge::Job job;
  bool taken = false;
  // The UI picks the request up on the first wait step but is slow to finish it.
  r.rig.onPause = [&] {
    if (!taken) {
      job = r.rig.bridge.takeJob();
      taken = true;
    }
  };
  TEST_ASSERT_FALSE(r.rig.bridge.begin(0, 3));  // 503 for the client
  ASSERT_ENUM_EQ(Bridge::Job::Prepare, job.kind);

  // The rest of that slow UI pass, as screenshotUiPass() does it.
  r.ret.onPrepare(r.shown, job.screen, kRealScreens);
  r.shown = job.screen;
  r.rig.bridge.finishJob(job, r.rig.now);  // too late: generation is stale
  const bool capturing = r.rig.bridge.capturing(r.rig.now);
  TEST_ASSERT_FALSE(capturing);
  const int back = r.ret.afterPass(capturing);
  TEST_ASSERT_EQUAL_INT(2, back);  // back on RADAR in the same pass
  r.shown = back;

  // Later the user taps TERAZ and a capture of that same screen runs: nothing
  // left over to jump back to.
  r.shown = 1;
  r.rig.onPause = [&r] { r.uiPass(); };
  r.captureAllBands(1);
  r.rig.bridge.end();
  r.uiPass();
  r.uiPass();
  TEST_ASSERT_EQUAL_INT(0, r.returns);
  TEST_ASSERT_EQUAL_INT(1, r.shown);
}

static void test_return_not_triggered_without_prepare(void) {
  ScreenshotReturn ret;
  TEST_ASSERT_EQUAL_INT(-1, ret.afterPass(false));
  TEST_ASSERT_EQUAL_INT(-1, ret.afterPass(true));
  TEST_ASSERT_EQUAL_INT(-1, ret.afterPass(false));
  // A demo index never arms a return either.
  ret.onPrepare(0, 4, kRealScreens);
  TEST_ASSERT_EQUAL_INT(-1, ret.afterPass(true));
  TEST_ASSERT_EQUAL_INT(-1, ret.afterPass(false));
}

// ---- screenshotUiPass() on a std::mutex bridge ----------------------------------
//
// Single-threaded: the handler's wait step (`pause`) runs one UI pass, standing
// in for core 1. A fake panel 4 px wide: every byte of row y is (y + 1).

struct PassMutex {
  std::mutex m;
  void lock() { m.lock(); }
  void unlock() { m.unlock(); }
};
using PassBridge = ScreenshotBridge<PassMutex>;

constexpr int kPassW = 4;
constexpr int kPassH = 8;
constexpr int kPassBandRows = 4;
constexpr size_t kPassRowBytes = 3 * kPassW;

struct PassRig {
  uint32_t now = 1000;
  bool uiOnPause = true;
  PassBridge bridge;
  ScreenshotReturn ret;
  int shown = kScreenFlights;
  std::vector<int> prepared;     // prepare() arguments
  std::vector<int> rowsRead;     // readRow() y arguments, in call order
  std::vector<bool> capturing;   // per pass
  std::vector<int> backTo;       // per pass
  uint8_t rowBuf[kPassRowBytes] = {};

  PassRig()
      : bridge(PassBridge::Hooks{[this] { return now; }, [] {},
                                 [this] {
                                   ++now;
                                   if (uiOnPause) uiPass();
                                 }}) {}
  PassRig(const PassRig &) = delete;
  PassRig &operator=(const PassRig &) = delete;

  void uiPass() {
    const ScreenshotPassResult r = screenshotUiPass(
        bridge, ret, shown, kScreenCount, now,
        [this](int screen) {
          prepared.push_back(screen);
          if (screen >= 0 && screen < kScreenCount) shown = screen;
        },
        [this](int y, uint8_t *bgr) {
          rowsRead.push_back(y);
          std::memset(bgr, y + 1, kPassRowBytes);
        },
        rowBuf, kPassRowBytes);
    capturing.push_back(r.capturing);
    backTo.push_back(r.backTo);
    if (r.backTo >= 0) shown = r.backTo;
  }
};

// Przejście UI obsługuje zrzut tą samą funkcją na płytce i w testach i wraca na poprzedni ekran
static void test_screenshot_ui_pass_returns_to_previous_screen(void) {
  PassRig rig;
  TEST_ASSERT_TRUE(rig.bridge.begin(kScreenRadar, 2000));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(rig.prepared.size()));
  TEST_ASSERT_EQUAL_INT(kScreenRadar, rig.prepared[0]);
  TEST_ASSERT_TRUE(rig.capturing.back());  // the pass that prepared it holds the screen

  const uint32_t stride = bmpRowStride(kPassW);
  std::vector<uint8_t> buf(stride * kPassBandRows, 0xA5);
  for (int k = 0; k < screenshotBandCount(kPassH, kPassBandRows); ++k) {
    const ScreenshotBand band = screenshotBand(kPassH, kPassBandRows, k);
    rig.rowsRead.clear();
    rig.bridge.requestBand(band.y0, band.rows, stride, buf.data());
    TEST_ASSERT_TRUE(rig.bridge.waitBand(2000));
    // The band's rows, from its bottom row up (BMP), each copied into the buffer.
    TEST_ASSERT_EQUAL_INT(band.rows, static_cast<int>(rig.rowsRead.size()));
    for (int i = 0; i < band.rows; ++i) {
      TEST_ASSERT_EQUAL_INT(band.y0 + band.rows - 1 - i, rig.rowsRead[static_cast<size_t>(i)]);
      TEST_ASSERT_EQUAL_UINT8(band.y0 + band.rows - i, buf[static_cast<size_t>(i) * stride]);
    }
    TEST_ASSERT_TRUE(rig.capturing.back());
    TEST_ASSERT_EQUAL_INT(kScreenRadar, rig.shown);
  }

  rig.bridge.end();
  rig.uiOnPause = false;
  const size_t before = rig.backTo.size();
  rig.uiPass();
  TEST_ASSERT_FALSE(rig.capturing.back());
  TEST_ASSERT_EQUAL_INT(kScreenFlights, rig.backTo.back());  // back to LOTY, exactly once
  rig.uiPass();
  rig.uiPass();
  TEST_ASSERT_EQUAL_INT(-1, rig.backTo.back());
  TEST_ASSERT_EQUAL_INT(-1, rig.backTo[before + 1]);
  TEST_ASSERT_EQUAL_INT(kScreenFlights, rig.shown);
}

}  // namespace

void run_screenshot_bridge_tests() {
  UnitySetTestFile(__FILE__);  // failures point at this file, not main.cpp

  RUN_TEST(test_screenshot_prepare_target_screens_and_demos);
  RUN_TEST(test_screenshot_prepare_target_out_of_range_keeps);
  RUN_TEST(test_screenshot_ui_pass_returns_to_previous_screen);

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
  RUN_TEST(test_screen_returns_to_previous_after_capture);
  RUN_TEST(test_screen_returns_after_abandoned_capture);
  RUN_TEST(test_no_return_when_capture_did_not_switch_screen);
  RUN_TEST(test_return_target_is_first_screen_of_capture_series);
  RUN_TEST(test_each_finished_capture_returns_to_its_own_previous_screen);
  RUN_TEST(test_screen_returns_when_prepare_finished_after_begin_timeout);
  RUN_TEST(test_return_not_triggered_without_prepare);
}
