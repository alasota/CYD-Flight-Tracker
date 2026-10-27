// version — the firmware's version string, e.g. "v1.2.0" (built from that
// git tag), "v1.2.0-3-g1a2b3c4" (3 commits after it), "1a2b3c4-dirty" (no tag
// yet, uncommitted changes). Set at build time by tools/pio_version.py from
// `git describe --tags --always --dirty` (or the FW_VERSION environment
// variable) and passed in as the FW_VERSION macro — to version.cpp only, so a
// new version doesn't recompile everything. Shown only in the config web page.
//
// Pure — tested under `pio test -e native` (where no version is injected).
#pragma once

// Fallback when the build didn't inject one (native tests, no git).
constexpr const char *kFirmwareVersionUnknown = "dev";

const char *firmwareVersion();

// Board variant the build is for, e.g. "cyd-r".
const char *firmwareBoard();

// "CYDSKY-FW1|board=<board>|v=<version>" — embedded in every build so an OTA
// upload can be recognised as this project's firmware (ota_update).
const char *firmwareMarker();
