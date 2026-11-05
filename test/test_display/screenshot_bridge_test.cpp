// screenshot_bridge: the UI side of /screenshot.bmp — what ?screen=N prepares, one UI pass of a capture. spec 09 §1, spec 10 §2.2.
#include <unity.h>

#include "display/services/screenshot_bridge.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "display/core/screen_nav.h"  // kScreenCount
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
}
