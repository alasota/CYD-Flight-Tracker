// screenshot — pure helpers for the config page's /screenshot.bmp endpoint:
// the 24-bit BMP header and row layout, and the ?screen= argument. The
// pixels themselves are read off the panel by main.cpp (LovyanGFX
// readRect()) and streamed by config_portal; nothing here touches hardware.
//
// BMP instead of PNG on purpose: a few lines of header code vs. pulling in
// a compressor, with flash ~96% full.
//
// Pure — tested under `pio test -e native`.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

constexpr int kBmpHeaderSize = 54;  // BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40)

// Bytes per BMP pixel row for a 24-bit image `width` px wide — 3 bytes per
// pixel, padded up to a multiple of 4.
uint32_t bmpRowStride(int width);

// Total file size: header + stride * height.
uint32_t bmpFileSize(int width, int height);

// Writes the 54-byte header for a 24-bit, uncompressed, bottom-up BMP
// (rows are then sent from the bottom row up, each as B,G,R bytes + padding).
void writeBmpHeader(uint8_t out[kBmpHeaderSize], int width, int height);

// The `screen` query argument: "0".."screenCount-1" -> that screen index;
// empty, non-numeric or out of range -> -1 ("capture whatever is showing").
int parseScreenshotScreenArg(const char *arg, int screenCount);

// The capture is read in bands of rows (the UI reads band k+1 off the panel
// while band k goes out over TCP — spec/done/04-dual-core.md §5.7). BMP is
// bottom-up, so bands run from the bottom of the screen: band `index`
// (0-based) covers rows y0..y0+rows-1, and they are sent from the last row of
// the band to the first. bandCount = ceil(height / bandRows).
struct ScreenshotBand {
  int y0 = 0;
  int rows = 0;
};
int screenshotBandCount(int height, int bandRows);
ScreenshotBand screenshotBand(int height, int bandRows, int index);

// ---- Handler <-> UI hand-over of one capture (spec 09 §1) -------------------
//
// The web handler (core 0) asks, the UI (core 1) reads the panel. Every
// request carries a generation; a timed-out or ended request sets the
// generation to 0, so a band the UI is still reading can neither write into
// the handler's (possibly freed) buffer nor mark itself done. Header-only,
// templated on the lock like LatestBox / Guarded (native tests: std::mutex).
template <typename Lock>
class ScreenshotBridge {
 public:
  enum State : int { Idle = 0, BeginAsked, BeginDone, BandAsked, BandDone };

  struct Hooks {
    std::function<uint32_t()> nowMs;   // device: millis()
    std::function<void()> wakeUi;      // device: core_link::wakeUi()
    std::function<void()> pause;       // one wait step (device: vTaskDelay(1))
  };
  static constexpr uint32_t kCaptureIdleMs = 5000;  // the client went away: draw again

  explicit ScreenshotBridge(Hooks h) : hooks_(std::move(h)) {}

  // ---- handler side (core 0)

  // Asks the UI to switch to `screenIndex` (-1 = keep) and stop drawing;
  // false = no answer within `timeoutMs` (the request is then dropped).
  bool begin(int screenIndex, uint32_t timeoutMs) {
    lock_.lock();
    gen_ = nextGeneration();
    screen_ = screenIndex;
    state_.store(BeginAsked);
    lock_.unlock();
    if (hooks_.wakeUi) hooks_.wakeUi();
    return waitFor(BeginDone, timeoutMs);
  }

  // Asks for `rows` BMP rows from `y0` into `out` (bottom row first); returns
  // at once — waitBand() waits.
  void requestBand(int y0, int rows, uint32_t stride, uint8_t *out) {
    lock_.lock();
    gen_ = nextGeneration();
    y0_ = y0;
    rows_ = rows;
    stride_ = stride;
    out_ = out;
    state_.store(BandAsked);
    lock_.unlock();
    if (hooks_.wakeUi) hooks_.wakeUi();
  }

  // False on timeout: the band is invalidated, the UI never writes into
  // `out` again, so the handler may free it.
  bool waitBand(uint32_t timeoutMs) { return waitFor(BandDone, timeoutMs); }

  // The capture is over (or abandoned): invalidates whatever is outstanding,
  // the UI draws again.
  void end() {
    lock_.lock();
    invalidate();
    lock_.unlock();
  }

  // ---- UI side (core 1)
  struct Job {
    enum Kind { None, Prepare, Band };
    Kind kind = None;
    int screen = -1;
    int y0 = 0;
    int rows = 0;
    uint32_t stride = 0;
    uint32_t gen = 0;
  };

  // What the handler asks for (None: nothing). Leaves the state alone —
  // finishJob() moves it on.
  Job takeJob() {
    Job job;
    lock_.lock();
    const int st = state_.load();
    if (st == BeginAsked || st == BandAsked) {
      job.kind = st == BeginAsked ? Job::Prepare : Job::Band;
      job.screen = screen_;
      job.y0 = y0_;
      job.rows = rows_;
      job.stride = stride_;
      job.gen = gen_;
    }
    lock_.unlock();
    return job;
  }

  // Copies row `i` of the job's band into the handler's buffer — only while
  // the job's generation is still current. False = abandon the band.
  bool writeRow(const Job &job, int i, const uint8_t *bgr, size_t n) {
    lock_.lock();
    const bool current = job.gen != 0 && gen_ == job.gen && out_ != nullptr;
    if (current) std::memcpy(out_ + static_cast<size_t>(i) * job.stride, bgr, n);
    lock_.unlock();
    return current;
  }

  // The job is done: BeginDone / BandDone — only if it is still the current
  // request (a late answer to a timed-out or ended one changes nothing).
  void finishJob(const Job &job, uint32_t nowMs) {
    if (job.kind == Job::None) return;
    lock_.lock();
    const bool current = job.gen != 0 && gen_ == job.gen;
    if (current) state_.store(job.kind == Job::Prepare ? BeginDone : BandDone);
    lock_.unlock();
    if (!current) return;
    if (job.kind == Job::Prepare) capturing_ = true;
    lastActivityMs_ = nowMs;
  }

  // True while a capture runs (the UI doesn't draw): false once Idle, or
  // more than kCaptureIdleMs since the last activity.
  bool capturing(uint32_t nowMs) {
    if (state_.load() == Idle) capturing_ = false;  // end() / timeout: draw again
    if (capturing_ && nowMs - lastActivityMs_ > kCaptureIdleMs) capturing_ = false;
    return capturing_;
  }

  State stateForTesting() const { return static_cast<State>(state_.load()); }
  // Starts the generation counter at `g` (wraparound tests).
  void setGenerationForTesting(uint32_t g) {
    lock_.lock();
    genCounter_ = g;
    lock_.unlock();
  }

 private:
  // Under lock_. Generation 0 means "nothing valid", so the wrap skips it.
  uint32_t nextGeneration() {
    if (++genCounter_ == 0) ++genCounter_;
    return genCounter_;
  }
  // Under lock_.
  void invalidate() {
    gen_ = 0;
    out_ = nullptr;
    state_.store(Idle);
  }

  // Handler side: polls `state_` (one `pause` per step) until `wanted`. On
  // timeout the request is invalidated under the lock — unless the UI
  // finished it at that very moment.
  bool waitFor(int wanted, uint32_t timeoutMs) {
    const uint32_t t0 = hooks_.nowMs();
    while (state_.load() != wanted) {
      if (hooks_.nowMs() - t0 > timeoutMs) {
        lock_.lock();
        const bool done = state_.load() == wanted;
        if (!done) invalidate();
        lock_.unlock();
        return done;
      }
      if (hooks_.pause) hooks_.pause();
    }
    return true;
  }

  Hooks hooks_;
  Lock lock_;
  // Shared: written under lock_ (state_ is also read without it).
  std::atomic<int> state_{Idle};
  uint32_t genCounter_ = 0;
  uint32_t gen_ = 0;  // the current request; 0 = none
  int screen_ = -1;
  int y0_ = 0;
  int rows_ = 0;
  uint32_t stride_ = 0;
  uint8_t *out_ = nullptr;
  // UI side only.
  bool capturing_ = false;
  uint32_t lastActivityMs_ = 0;
};

// ---- Back to the screen shown before the capture (spec 10 §2.2) -------------
//
// /screenshot.bmp?screen=N switches the display only for the capture. The UI
// (core 1) calls onPrepare() when it answers the Prepare job and afterPass()
// with ScreenshotBridge::capturing() on every pass; afterPass() returns the
// screen to go back to exactly once, on the first pass that isn't capturing
// (end(), timeout or the 5 s idle rule), else -1. Not tied to a capturing
// true -> false edge: a Prepare whose begin() timed out before the UI finished
// it never makes capturing() true, and the switched screen must still come
// back (and not linger as a stale target for a later capture). Only a real switch
// (requested in 0..screenCount-1 and != current) is remembered — no screen
// argument (-1), the same screen and the demo indices (>= screenCount) leave
// nothing to return to. UI side only, no lock.
class ScreenshotReturn {
 public:
  // A capture series (begin() again before the UI saw the previous end())
  // keeps the screen shown before its first capture.
  void onPrepare(int currentScreen, int requestedScreen, int screenCount) {
    if (requestedScreen < 0 || requestedScreen >= screenCount) return;  // none / demo
    if (requestedScreen == currentScreen || armed_) return;
    armed_ = true;
    back_ = currentScreen;
  }

  int afterPass(bool capturing) {
    if (capturing || !armed_) return -1;
    armed_ = false;
    return back_;
  }

 private:
  bool armed_ = false;
  int back_ = -1;
};
