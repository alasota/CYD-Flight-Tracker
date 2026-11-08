// config_page_writer — the settings page's HTML writer: pieces, translated
// strings and the form controls the tabs repeat (a labelled field, a select,
// the language picker, a section's opening tag). Everything goes to the
// page's HtmlSink; config_page and config_page_tabs share it.
//
// Pure — tested through renderConfigPage() under `pio test -e native`.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "admin/core/config_page.h"

namespace config_page_detail {

class Writer {
 public:
  Writer(const HtmlSink &emit, Language lang) : emit_(emit), lang_(lang) {}
  void raw(const char *s) {
    if (s != nullptr && *s != '\0') emit_(s);
  }
  void raw(const std::string &s) { raw(s.c_str()); }
  void esc(const std::string &s) { raw(htmlEscape(s)); }
  void t(WebStr id) { raw(webTr(id, lang_)); }
  void uint(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
    raw(buf);
  }

  // <div class=f><label for=ID>LABEL</label><input ... value="VALUE"></div>
  // `attrs` = everything after "<input " up to the value.
  void field(const char *id, WebStr label, const char *attrs, const std::string &value,
             const WebStr *hint = nullptr) {
    raw("<div class=\"f\"><label for=\"");
    raw(id);
    raw("\">");
    t(label);
    raw("</label><input id=\"");
    raw(id);
    raw("\" ");
    raw(attrs);
    raw(" value=\"");
    esc(value);
    raw("\">");
    if (hint != nullptr) {
      raw("<div class=\"hint\">");
      t(*hint);
      raw("</div>");
    }
    raw("</div>");
  }

  void languageSelect(const char *id, const char *name, WebStr label, Language selected) {
    raw("<div class=\"f\"><label for=\"");
    raw(id);
    raw("\">");
    t(label);
    raw("</label><select id=\"");
    raw(id);
    raw("\" name=\"");
    raw(name);
    raw("\">");
    raw(selected == Language::PL ? "<option value=\"pl\" selected>Polski</option>"
                                 : "<option value=\"pl\">Polski</option>");
    raw(selected == Language::EN ? "<option value=\"en\" selected>English</option>"
                                 : "<option value=\"en\">English</option>");
    raw("</select></div>");
  }

  struct Option {
    const char *value;
    WebStr label;
  };
  // <div class=f><label>LABEL</label><select id name>...options...</select></div>
  void select(const char *id, const char *name, WebStr label, const Option *options, int count,
              const char *selected) {
    raw("<div class=\"f\"><label for=\"");
    raw(id);
    raw("\">");
    t(label);
    raw("</label><select id=\"");
    raw(id);
    raw("\" name=\"");
    raw(name);
    raw("\">");
    for (int i = 0; i < count; ++i) {
      raw("<option value=\"");
      raw(options[i].value);
      raw(std::string(options[i].value) == selected ? "\" selected>" : "\">");
      t(options[i].label);
      raw("</option>");
    }
    raw("</select></div>");
  }

  void sectionOpen(SettingsTab tab, SettingsTab active) {
    raw("<section data-tab=\"");
    raw(settingsTabId(tab));
    raw(tab == active ? "\">" : "\" hidden>");
  }

 private:
  const HtmlSink &emit_;
  Language lang_;
};

inline std::string fmt(const char *f, double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), f, v);
  return buf;
}
inline std::string fmtU(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
  return buf;
}

}  // namespace config_page_detail
