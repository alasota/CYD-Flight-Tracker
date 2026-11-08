// language — the languages of the display and the settings page (Polish,
// English). A shared kernel type: contexts that only need to take a language
// as a parameter include this instead of display/core/i18n.h, the display's
// string tables. See CLAUDE.md "Language".
//
// Pure — no Arduino dependency.
#pragma once

#include <cstdint>

enum class Language : uint8_t {
  PL = 0,
  EN = 1,
};
constexpr int kLanguageCount = 2;
constexpr Language kDefaultLanguage = Language::PL;
