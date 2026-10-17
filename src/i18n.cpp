#include "i18n.h"

#include <cstring>

namespace {

#define I18N_PL_ENTRY(id, pl, en) pl,
#define I18N_EN_ENTRY(id, pl, en) en,
const char *const kPolish[kStrCount] = {I18N_STRINGS(I18N_PL_ENTRY)};
const char *const kEnglish[kStrCount] = {I18N_STRINGS(I18N_EN_ENTRY)};
#undef I18N_PL_ENTRY
#undef I18N_EN_ENTRY

const char *const *const kTables[kLanguageCount] = {kPolish, kEnglish};

const char *const kCodes[kLanguageCount] = {"pl", "en"};

Language currentLanguage = kDefaultLanguage;

}  // namespace

const char *tr(Str id, Language lang) {
  const int i = static_cast<int>(id);
  const int l = static_cast<int>(lang);
  if (i < 0 || i >= kStrCount || l < 0 || l >= kLanguageCount) return "?";
  return kTables[l][i];
}

const char *tr(Str id) { return tr(id, currentLanguage); }

void i18nSetLanguage(Language lang) { currentLanguage = clampLanguage(static_cast<int>(lang)); }

Language i18nLanguage() { return currentLanguage; }

const char *languageCode(Language lang) {
  const int l = static_cast<int>(lang);
  return (l >= 0 && l < kLanguageCount) ? kCodes[l] : kCodes[static_cast<int>(kDefaultLanguage)];
}

Language languageFromCode(const char *code) {
  if (code != nullptr) {
    for (int l = 0; l < kLanguageCount; ++l) {
      if (std::strcmp(code, kCodes[l]) == 0) return static_cast<Language>(l);
    }
  }
  return kDefaultLanguage;
}

Language clampLanguage(int raw) {
  return (raw >= 0 && raw < kLanguageCount) ? static_cast<Language>(raw) : kDefaultLanguage;
}
