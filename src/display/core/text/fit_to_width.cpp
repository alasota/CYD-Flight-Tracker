#include "display/core/text/fit_to_width.h"

#include "display/core/text/utf8.h"

std::string fitToWidth(const std::string &text, int maxPx, const TextMeasure &measure) {
  if (maxPx <= 0) return std::string();
  if (measure(text) <= maxPx) return text;

  // Longest head + "..." that fits (head must keep at least one char).
  for (size_t n = text.size() - 1; n >= 1; --n) {
    if (!utf8IsCharBoundary(text, n)) continue;  // never split a UTF-8 letter
    std::string head = text.substr(0, n);
    while (!head.empty() && head.back() == ' ') head.pop_back();
    if (head.empty()) continue;
    std::string candidate = head + "...";
    if (measure(candidate) <= maxPx) return candidate;
  }

  // Too tight for an ellipsis — longest plain head that fits.
  for (size_t n = text.size() - 1; n >= 1; --n) {
    if (!utf8IsCharBoundary(text, n)) continue;
    std::string head = text.substr(0, n);
    if (measure(head) <= maxPx) return head;
  }
  return std::string();
}
