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
  // TODO(spec 09): stub from test-author
  bool begin(int /*screenIndex*/, uint32_t /*timeoutMs*/) { return false; }
  // TODO(spec 09): stub from test-author
  void requestBand(int /*y0*/, int /*rows*/, uint32_t /*stride*/, uint8_t * /*out*/) {}
  // TODO(spec 09): stub from test-author
  bool waitBand(uint32_t /*timeoutMs*/) { return false; }
  // TODO(spec 09): stub from test-author
  void end() {}

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
  // TODO(spec 09): stub from test-author
  Job takeJob() { return {}; }
  // Copies row `i` of the job's band into the handler's buffer — only while
  // the job's generation is still current. False = abandon the band.
  // TODO(spec 09): stub from test-author
  bool writeRow(const Job & /*job*/, int /*i*/, const uint8_t * /*bgr*/, size_t /*n*/) {
    return false;
  }
  // TODO(spec 09): stub from test-author
  void finishJob(const Job & /*job*/, uint32_t /*nowMs*/) {}
  // True while a capture runs (the UI doesn't draw): false once Idle, or
  // more than kCaptureIdleMs since the last activity.
  // TODO(spec 09): stub from test-author
  bool capturing(uint32_t /*nowMs*/) { return false; }

  // TODO(spec 09): stub from test-author
  State stateForTesting() const { return Idle; }
  // Starts the generation counter at `g` (wraparound tests).
  // TODO(spec 09): stub from test-author
  void setGenerationForTesting(uint32_t /*g*/) {}

 private:
  Hooks hooks_;
  Lock lock_;
};
