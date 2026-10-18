// touch_calibration — this board's XPT2046 touch calibration, in
// LovyanGFX's terms. Shared by include/LGFX_CYD.hpp (which hands it to
// lgfx::Touch_XPT2046) and touch_input's pure mapRawTouch() model (which
// the native tests check against raw taps logged on the real board), so the
// two can't drift apart. Pure constants — no Arduino/LovyanGFX dependency.
//
// LovyanGFX raw axes on the CYD (landscape, setRotation(1)):
//   - raw X = the screen's *vertical* axis: ~340 at the bottom edge .. ~3900
//     at the top.
//   - raw Y = the screen's *horizontal* axis, and it runs RIGHT-TO-LEFT:
//     ~3900 at the left edge .. ~200 at the right.
// Compared with the old XPT2046_Touchscreen library (rotation 1), LovyanGFX
// reports the channels swapped *and* the horizontal one inverted:
// LovyanGFX raw Y = 4095 - old raw X. The first port only swapped them,
// which mirrored the buttons left<->right (LOTY opened RADAR and vice versa)
// while the middle one still worked — logged 2026-10-18: taps on the left
// button read raw Y ~3200-3300, the right one ~700.
//
// With offset_rotation = 0 and setRotation(1), LovyanGFX swaps the axes and
// flips the vertical itself; the horizontal inversion is expressed by giving
// it y_min > y_max (its calibration is an affine fit through the four corner
// points, so a reversed range is just a mirrored axis).
//
// Bottom-nav buttons at y~227 read raw X ~400..660. Corners weren't
// measured, so the extreme edges are an estimate — re-tune from the [touch]
// Serial log if taps near an edge feel off.
#pragma once

#include <cstdint>

constexpr uint16_t kTouchRawXAtBottom = 340;   // vertical axis
constexpr uint16_t kTouchRawXAtTop = 3900;
constexpr uint16_t kTouchRawYAtLeft = 3900;    // horizontal axis (inverted!)
constexpr uint16_t kTouchRawYAtRight = 200;
constexpr uint8_t kTouchOffsetRotation = 0;
