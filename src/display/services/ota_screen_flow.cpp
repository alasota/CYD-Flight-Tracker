#include "display/services/ota_screen_flow.h"

// TODO(spec 12): stub from test-author
OtaScreenStep OtaScreenFlow::step(const OtaProgress & /*o*/, bool /*restartPending*/,
                                  uint32_t /*nowMs*/) {
  return {};
}

// TODO(spec 12): stub from test-author
bool OtaScreenFlow::holdsBacklight(const OtaProgress & /*o*/, bool /*restartPending*/) const {
  return false;
}
