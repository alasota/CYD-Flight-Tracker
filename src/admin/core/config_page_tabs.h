// config_page_tabs — the settings page's five tabs, one function each, in the
// order the page shows them. config_page writes the head, the tab bar and the
// footer around them.
//
// Pure — tested through renderConfigPage() under `pio test -e native`.
#pragma once

#include <string>

#include "admin/core/config_page.h"
#include "admin/core/config_page_writer.h"

namespace config_page_detail {

void locationTab(Writer &w, const ConfigPageModel &m);
void openSkyTab(Writer &w, const ConfigPageModel &m, Language lang);
void generalTab(Writer &w, const ConfigPageModel &m, Language lang);
void backupTab(Writer &w, const ConfigPageModel &m);
void firmwareTab(Writer &w, const ConfigPageModel &m, const std::string &installed);

}  // namespace config_page_detail
