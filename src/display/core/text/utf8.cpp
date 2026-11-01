#include "display/core/text/utf8.h"

bool utf8IsCharBoundary(const std::string &s, size_t i) {
  return i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
}

size_t utf8Length(const std::string &utf8) {
  size_t n = 0;
  for (size_t i = 0; i < utf8.size(); ++i) {
    if (utf8IsCharBoundary(utf8, i)) ++n;
  }
  return n;
}
