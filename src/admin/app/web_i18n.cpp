#include "admin/app/web_i18n.h"

namespace {

static const char *const kPolish[] = {
#define WEB_I18N_PL(id, pl, en) pl,
    WEB_I18N_STRINGS(WEB_I18N_PL)
#undef WEB_I18N_PL
};

static const char *const kEnglish[] = {
#define WEB_I18N_EN(id, pl, en) en,
    WEB_I18N_STRINGS(WEB_I18N_EN)
#undef WEB_I18N_EN
};

static_assert(sizeof(kPolish) / sizeof(kPolish[0]) == kWebStrCount, "PL table out of step");
static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == kWebStrCount, "EN table out of step");

}  // namespace

const char *webTr(WebStr id, Language lang) {
  const int i = static_cast<int>(id);
  if (i < 0 || i >= kWebStrCount) return "";
  return lang == Language::EN ? kEnglish[i] : kPolish[i];
}
