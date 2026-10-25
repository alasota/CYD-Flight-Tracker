#include "screenshot.h"

namespace {

void put16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

}  // namespace

uint32_t bmpRowStride(int width) {
  const uint32_t raw = static_cast<uint32_t>(width) * 3u;
  return (raw + 3u) & ~3u;
}

uint32_t bmpFileSize(int width, int height) {
  return static_cast<uint32_t>(kBmpHeaderSize) + bmpRowStride(width) * static_cast<uint32_t>(height);
}

void writeBmpHeader(uint8_t out[kBmpHeaderSize], int width, int height) {
  for (int i = 0; i < kBmpHeaderSize; ++i) out[i] = 0;

  // BITMAPFILEHEADER
  out[0] = 'B';
  out[1] = 'M';
  put32(&out[2], bmpFileSize(width, height));
  put32(&out[10], kBmpHeaderSize);  // offset to pixel data

  // BITMAPINFOHEADER
  put32(&out[14], 40);
  put32(&out[18], static_cast<uint32_t>(width));
  put32(&out[22], static_cast<uint32_t>(height));  // positive -> bottom-up
  put16(&out[26], 1);                              // planes
  put16(&out[28], 24);                             // bits per pixel
  put32(&out[30], 0);                              // BI_RGB
  put32(&out[34], bmpRowStride(width) * static_cast<uint32_t>(height));
  // resolution / palette fields stay 0
}

int parseScreenshotScreenArg(const char *arg, int screenCount) {
  if (arg == nullptr || arg[0] == '\0') return -1;
  int value = 0;
  for (const char *p = arg; *p; ++p) {
    if (*p < '0' || *p > '9') return -1;
    value = value * 10 + (*p - '0');
    if (value >= screenCount) return -1;
  }
  return value;
}

int screenshotBandCount(int height, int bandRows) {
  if (height <= 0 || bandRows <= 0) return 0;
  return (height + bandRows - 1) / bandRows;
}

ScreenshotBand screenshotBand(int height, int bandRows, int index) {
  ScreenshotBand b;
  if (index < 0 || index >= screenshotBandCount(height, bandRows)) return b;
  const int top = height - (index + 1) * bandRows;  // may run past row 0 on the last band
  b.y0 = top < 0 ? 0 : top;
  b.rows = height - index * bandRows - b.y0;
  return b;
}
