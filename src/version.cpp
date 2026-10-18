#include "version.h"

const char *firmwareVersion() {
#ifdef FW_VERSION
  return FW_VERSION;
#else
  return kFirmwareVersionUnknown;
#endif
}
