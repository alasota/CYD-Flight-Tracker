// touch_calibration — this board's XPT2046 touch calibration, in
// LovyanGFX's terms. Shared by include/LGFX_CYD.hpp (which hands it to
// lgfx::Touch_XPT2046) and touch_input's pure mapRawTouch() model (which
// the native tests check against raw taps logged on the real board), so the
// two can't drift apart. Pure constants — no Arduino/LovyanGFX dependency.
//
// LovyanGFX raw axes on the CYD (Touch_XPT2046 reads the controller's
// channels the other way round from the old XPT2046_Touchscreen library):
//   - raw X = the screen's *vertical* axis: ~340 at the bottom edge .. ~3900
//     at the top (in landscape, setRotation(1)).
//   - raw Y = the screen's *horizontal* axis: ~200 at the left edge .. ~3900
//     at the right.
// With offset_rotation = 0 and setRotation(1), LovyanGFX swaps the axes and
// flips the vertical one itself, so the min/max values below are plain
// (min < max) ranges.
//
// Measured 2026-10-17 from taps on the three bottom-nav buttons (logged raw
// values: buttons at y~227 read raw X ~400..660; left/middle/right buttons
// read raw Y ~850 / ~2000 / ~3100). Corners weren't measured, so the extreme
// edges are an estimate — re-tune from the [touch] Serial log if taps near
// an edge feel off.
#pragma once

#include <cstdint>

constexpr uint16_t kTouchRawXMin = 340;   // bottom edge (vertical axis)
constexpr uint16_t kTouchRawXMax = 3900;  // top edge
constexpr uint16_t kTouchRawYMin = 200;   // left edge (horizontal axis)
constexpr uint16_t kTouchRawYMax = 3900;  // right edge
constexpr uint8_t kTouchOffsetRotation = 0;
