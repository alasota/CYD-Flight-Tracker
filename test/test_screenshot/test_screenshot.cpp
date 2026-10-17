#include <unity.h>

#include "screenshot.h"

void setUp(void) {}
void tearDown(void) {}

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

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_row_stride_320_needs_no_padding);
  RUN_TEST(test_row_stride_pads_to_multiple_of_4);
  RUN_TEST(test_file_size_for_the_cyd_screen);
  RUN_TEST(test_header_fields);
  RUN_TEST(test_header_size_constant);
  RUN_TEST(test_parse_screen_arg);

  return UNITY_END();
}
