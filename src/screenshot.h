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

#include <cstdint>

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
