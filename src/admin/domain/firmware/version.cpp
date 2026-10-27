#include "admin/domain/firmware/version.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"  // kFirmwareVersionUnknown — native tests, no git
#endif

// Board variant this build is for (CYD "R": ILI9341 + resistive touch).
// Roadmap #4 will set it per PlatformIO env.
#ifndef FW_BOARD
#define FW_BOARD "cyd-r"
#endif

// Found by ota_update's FirmwareMarkerScanner in an uploaded .bin: tells this
// project's firmware apart from any other ESP32 image. Fixed format — the
// scanner must keep understanding it. `used` + the reference in
// firmwareMarker() keep the linker (--gc-sections) from dropping it.
extern "C" __attribute__((used)) const char kFirmwareMarker[] =
    "CYDSKY-FW1|board=" FW_BOARD "|v=" FW_VERSION;

const char *firmwareVersion() { return FW_VERSION; }

const char *firmwareBoard() { return FW_BOARD; }

const char *firmwareMarker() { return kFirmwareMarker; }
